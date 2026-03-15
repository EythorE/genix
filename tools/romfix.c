/*
 * romfix — post-process a Mega Drive ROM to resolve XIP addresses
 *
 * Usage: romfix <rom.bin> <romdisk_offset> <user_base>
 *
 * Parses the minifs filesystem embedded in the ROM at the given offset,
 * finds all Genix binaries with relocation tables, and resolves all
 * absolute addresses in-place:
 *   - text references → absolute ROM addresses
 *   - data references → USER_BASE addresses
 *
 * After processing, each binary's header has GENIX_FLAG_XIP set and
 * relocations are consumed (reloc_count zeroed). The kernel's XIP
 * loader can then execute text directly from ROM.
 *
 * This is Strategy A from docs/relocatable-binaries.md: build-time
 * resolved XIP with zero runtime relocation cost.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Must match kernel definitions */
#define BLOCK_SIZE  1024
#define GENIX_MAGIC 0x47454E58
#define GENIX_HDR_SIZE 32
#define GENIX_FLAG_XIP 0x01
#define MINIFS_MAGIC 0x4D494E49
#define NAME_MAX_FS 30

#define FT_FREE 0
#define FT_FILE 1
#define FT_DIR  2

#define INODES_PER_BLK (BLOCK_SIZE / 48)  /* 21 */
#define DIRENTS_PER_BLK (BLOCK_SIZE / 32) /* 32 */

/* Big-endian read/write helpers (ROM is 68000 big-endian) */
static uint16_t get16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* ROM image */
static uint8_t *rom;
static size_t rom_size;
static uint32_t romdisk_off;  /* byte offset of romdisk in ROM */
static uint32_t user_base;    /* USER_BASE for data references */

/* Get a pointer to a romdisk block */
static uint8_t *rom_block(uint16_t blkno)
{
    uint32_t off = romdisk_off + (uint32_t)blkno * BLOCK_SIZE;
    if (off + BLOCK_SIZE > rom_size)
        return NULL;
    return rom + off;
}

/* Read a disk inode from the romdisk */
struct disk_inode {
    uint8_t  type;
    uint8_t  nlink;
    uint8_t  dev_major;
    uint8_t  dev_minor;
    uint32_t size;
    uint32_t mtime;
    uint16_t direct[12];
    uint16_t indirect;
};

static int read_inode(uint16_t inum, struct disk_inode *di)
{
    /* inode block = 1 + (inum-1) / INODES_PER_BLK */
    int blk = 1 + (inum - 1) / INODES_PER_BLK;
    int off = ((inum - 1) % INODES_PER_BLK) * 48;

    uint8_t *p = rom_block(blk);
    if (!p) return -1;
    p += off;

    di->type = p[0];
    di->nlink = p[1];
    di->dev_major = p[2];
    di->dev_minor = p[3];
    di->size = get32(p + 4);
    di->mtime = get32(p + 8);
    for (int i = 0; i < 12; i++)
        di->direct[i] = get16(p + 12 + i * 2);
    di->indirect = get16(p + 36);
    return 0;
}

/*
 * Get the ROM byte address of a file's data start.
 * Returns 0 if blocks are not contiguous or file is empty.
 *
 * For files using indirect blocks (>12 blocks), reads the indirect
 * block to verify that all data blocks are contiguous.  mkfs allocates
 * sequentially so this is normally the case.
 */
static uint32_t file_rom_addr(struct disk_inode *di)
{
    if (di->size == 0 || di->direct[0] == 0)
        return 0;

    uint32_t nblocks = (di->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint16_t first = di->direct[0];

    /* Check contiguity of direct blocks */
    uint32_t check = nblocks < 12 ? nblocks : 12;
    for (uint32_t i = 1; i < check; i++) {
        if (di->direct[i] != first + i)
            return 0;
    }

    /* For files using indirect blocks, verify those are contiguous too */
    if (nblocks > 12) {
        if (di->indirect == 0)
            return 0;
        uint8_t *indirect = rom_block(di->indirect);
        if (!indirect)
            return 0;
        for (uint32_t i = 12; i < nblocks; i++) {
            uint16_t blkno = get16(indirect + (i - 12) * 2);
            if (blkno != first + i)
                return 0;
        }
    }

    return romdisk_off + (uint32_t)first * BLOCK_SIZE;
}

/*
 * Get a pointer to file data at the given file offset.
 * Only works for contiguous files (verified by file_rom_addr).
 */
static uint8_t *file_data_ptr(uint32_t rom_base, uint32_t file_off)
{
    uint32_t addr = rom_base + file_off;
    if (addr >= rom_size)
        return NULL;
    return rom + addr;
}

/*
 * Process a single Genix binary in the ROM disk.
 * Applies XIP relocations and sets the XIP flag.
 */
static int process_binary(const char *name, struct disk_inode *di)
{
    uint32_t rom_base = file_rom_addr(di);
    if (rom_base == 0) {
        fprintf(stderr, "  %s: blocks not contiguous, skipping\n", name);
        return 0;
    }

    /* Read header (big-endian) */
    uint8_t *hdr_ptr = file_data_ptr(rom_base, 0);
    if (!hdr_ptr || di->size < GENIX_HDR_SIZE)
        return 0;

    uint32_t magic = get32(hdr_ptr + 0);
    if (magic != GENIX_MAGIC) {
        fprintf(stderr, "  %s: not a Genix binary (magic 0x%08x)\n",
                name, magic);
        return 0;
    }

    uint32_t load_size   = get32(hdr_ptr + 4);
    uint32_t bss_size    = get32(hdr_ptr + 8);
    uint32_t entry       = get32(hdr_ptr + 12);
    uint32_t stack_field = get32(hdr_ptr + 16);
    uint32_t flags       = get32(hdr_ptr + 20);
    uint32_t text_size   = get32(hdr_ptr + 24);
    uint32_t reloc_count = get32(hdr_ptr + 28);

    (void)bss_size;
    (void)entry;

    /* Detect -msep-data binaries by GOT offset in upper 16 bits of stack_size */
    uint32_t got_offset = stack_field >> 16;

    /* Already XIP-resolved? */
    if (flags & GENIX_FLAG_XIP) {
        printf("  %s: already XIP, skipping\n", name);
        return 0;
    }

    /* Need text_size for XIP split relocation */
    if (text_size == 0) {
        fprintf(stderr, "  %s: text_size=0, can't XIP (no split info)\n",
                name);
        return 0;
    }

    if (text_size > load_size) {
        fprintf(stderr, "  %s: text_size > load_size, skipping\n", name);
        return 0;
    }

    if (reloc_count == 0) {
        /* No relocations — still mark as XIP since text can run from ROM.
         * This handles programs with no absolute references. */
        printf("  %s: no relocs, marking XIP (text=%u, data=%u)\n",
               name, text_size, load_size - text_size);
        put32(hdr_ptr + 20, flags | GENIX_FLAG_XIP);
        return 1;
    }

    /* Compute addresses:
     *   text_base = ROM address of text segment
     *   data_base = USER_BASE (where data will be loaded at runtime) */
    uint32_t text_base = rom_base + GENIX_HDR_SIZE;
    uint32_t data_base = user_base;

    /* Read and apply relocations.
     * Relocation table is after header + load_size in the file. */
    uint32_t reloc_file_off = GENIX_HDR_SIZE + load_size;
    uint8_t *reloc_ptr = file_data_ptr(rom_base, reloc_file_off);
    if (!reloc_ptr) {
        fprintf(stderr, "  %s: reloc table out of bounds\n", name);
        return -1;
    }

    int patched = 0;
    int skipped = 0;
    int deferred = 0;

    for (uint32_t i = 0; i < reloc_count; i++) {
        uint32_t off = get32(reloc_ptr + i * 4);

        /* Validate offset */
        if ((off & 1) || off + 4 > load_size) {
            skipped++;
            continue;
        }

        /* For -msep-data binaries (got_offset > 0), skip data-segment
         * relocations. These are GOT entries that need runtime patching
         * with the actual slot base address, which varies per process.
         * Only resolve text-segment relocations (in ROM) at build time. */
        if (got_offset > 0 && off >= text_size) {
            deferred++;
            continue;
        }

        /* Locate the word to patch in the ROM image */
        uint8_t *ptr = file_data_ptr(rom_base, GENIX_HDR_SIZE + off);

        if (!ptr) {
            skipped++;
            continue;
        }

        /* Read the zero-based value and determine what it references */
        uint32_t val = get32(ptr);
        uint32_t new_val;

        if (val < text_size)
            new_val = val + text_base;     /* references text → ROM */
        else
            new_val = (val - text_size) + data_base;  /* references data → RAM */

        put32(ptr, new_val);
        patched++;
    }

    /* Set XIP flag in header */
    put32(hdr_ptr + 20, flags | GENIX_FLAG_XIP);

    printf("  %s: XIP resolved — text=%u @ 0x%x, data=%u → 0x%x, "
           "%d relocs patched",
           name, text_size, text_base,
           load_size - text_size, data_base, patched);
    if (deferred > 0)
        printf(", %d deferred (runtime)", deferred);
    if (skipped > 0)
        printf(", %d skipped", skipped);
    printf("\n");

    return 1;
}

/*
 * Scan the /bin directory and process all Genix binaries.
 */
static int scan_directory(uint16_t dir_inum)
{
    struct disk_inode dir_di;
    if (read_inode(dir_inum, &dir_di) < 0)
        return -1;

    if (dir_di.type != FT_DIR)
        return -1;

    int processed = 0;
    uint32_t off = 0;

    while (off < dir_di.size) {
        /* Find which block this dirent is in */
        uint32_t blk_idx = off / BLOCK_SIZE;
        uint32_t blk_off = off % BLOCK_SIZE;
        uint16_t blkno;

        if (blk_idx < 12) {
            blkno = dir_di.direct[blk_idx];
        } else {
            /* Read from indirect block */
            uint8_t *indirect = rom_block(dir_di.indirect);
            if (!indirect) break;
            int ioff = (blk_idx - 12) * 2;
            blkno = get16(indirect + ioff);
        }

        uint8_t *blk = rom_block(blkno);
        if (!blk) break;

        uint8_t *de = blk + blk_off;
        uint16_t child_inum = get16(de);
        char name[NAME_MAX_FS + 2];
        memset(name, 0, sizeof(name));
        memcpy(name, de + 2, NAME_MAX_FS);

        off += 32;  /* sizeof(dirent_disk) */

        if (child_inum == 0)
            continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        struct disk_inode child_di;
        if (read_inode(child_inum, &child_di) < 0)
            continue;

        if (child_di.type == FT_DIR) {
            /* Recurse into subdirectories */
            printf("Scanning /%s:\n", name);
            processed += scan_directory(child_inum);
        } else if (child_di.type == FT_FILE) {
            int rc = process_binary(name, &child_di);
            if (rc > 0)
                processed += rc;
        }
    }

    return processed;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <rom.bin> <romdisk_offset> <user_base>\n",
                argv[0]);
        fprintf(stderr, "  romdisk_offset: byte offset of romdisk in ROM (hex ok)\n");
        fprintf(stderr, "  user_base: USER_BASE address for data refs (hex ok)\n");
        return 1;
    }

    const char *rom_path = argv[1];
    romdisk_off = strtoul(argv[2], NULL, 0);
    user_base = strtoul(argv[3], NULL, 0);

    /* Read ROM file */
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        perror(rom_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    rom = malloc(rom_size);
    if (!rom) {
        fprintf(stderr, "Out of memory (ROM is %zu bytes)\n", rom_size);
        fclose(f);
        return 1;
    }
    if (fread(rom, 1, rom_size, f) != rom_size) {
        fprintf(stderr, "Short read on %s\n", rom_path);
        free(rom);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* Validate romdisk superblock */
    uint8_t *sb = rom_block(0);
    if (!sb) {
        fprintf(stderr, "Romdisk offset 0x%x is out of bounds\n", romdisk_off);
        free(rom);
        return 1;
    }
    uint32_t sb_magic = get32(sb);
    if (sb_magic != MINIFS_MAGIC) {
        fprintf(stderr, "No minifs superblock at offset 0x%x (got 0x%08x)\n",
                romdisk_off, sb_magic);
        free(rom);
        return 1;
    }

    uint16_t nblocks = get16(sb + 4 + 2);
    uint16_t ninodes = get16(sb + 4 + 2 + 2);
    printf("romfix: ROM %s, romdisk at 0x%x (%d blocks, %d inodes)\n",
           rom_path, romdisk_off, nblocks, ninodes);
    printf("romfix: USER_BASE = 0x%x\n", user_base);

    /* Scan root directory (inode 1) and process all files */
    printf("\nScanning filesystem:\n");
    int processed = scan_directory(1);

    if (processed > 0) {
        /* Write modified ROM back */
        f = fopen(rom_path, "wb");
        if (!f) {
            perror(rom_path);
            free(rom);
            return 1;
        }
        if (fwrite(rom, 1, rom_size, f) != rom_size) {
            fprintf(stderr, "Short write on %s\n", rom_path);
            fclose(f);
            free(rom);
            return 1;
        }
        fclose(f);
        printf("\nromfix: %d binaries XIP-resolved, ROM updated.\n", processed);
    } else {
        printf("\nromfix: no binaries processed (all already XIP or no relocs).\n");
    }

    free(rom);
    return 0;
}
