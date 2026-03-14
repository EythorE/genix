/*
 * mkbin — Convert m68k ELF to Genix relocatable flat binary
 *
 * Reads an ELF executable (linked at address 0 with --emit-relocs),
 * extracts loadable segments and R_68K_32 relocation entries, and
 * produces a Genix binary with a 32-byte header + relocation table.
 *
 * Binary layout:
 *   [32-byte header]
 *   [text+data: load_size bytes]
 *   [relocation table: reloc_count * 4 bytes]
 *
 * Each relocation entry is a big-endian uint32_t offset from the
 * start of the loaded image. The 32-bit word at that offset contains
 * a zero-based absolute address that must have the load address added.
 *
 * Usage: mkbin input.elf output
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Genix binary header (must match kernel/kernel.h) */
#define GENIX_MAGIC  0x47454E58  /* "GENX" */

struct genix_header {
    uint32_t magic;
    uint32_t load_size;    /* text+data bytes */
    uint32_t bss_size;     /* BSS bytes to zero */
    uint32_t entry;        /* entry offset (0-based) */
    uint32_t stack_size;   /* bits 0-15: stack hint, bits 16-31: GOT offset from data start */
    uint32_t flags;        /* GENIX_FLAG_XIP etc. */
    uint32_t text_size;    /* text segment size (for split reloc / XIP) */
    uint32_t reloc_count;  /* number of relocation entries */
};

/* ELF constants */
#define EI_NIDENT    16
#define ELFMAG       "\177ELF"
#define ELFCLASS32   1
#define ELFDATA2MSB  2
#define EM_68K       4
#define PT_LOAD      1
#define SHT_RELA     4
#define SHT_PROGBITS 1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4

/* m68k relocation types */
#define R_68K_32     1   /* absolute 32-bit */
#define R_68K_16     2   /* absolute 16-bit (error) */
#define R_68K_PC32   4   /* PC-relative 32-bit (skip) */
#define R_68K_PC16   5   /* PC-relative 16-bit (skip) */
#define R_68K_PC8    6   /* PC-relative 8-bit (skip) */
#define R_68K_GOT16O 11  /* GOT offset 16-bit (skip, resolved at link) */

#define SHT_STRTAB   3   /* string table */

/* Big-endian read helpers */
static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Big-endian write helper */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static uint8_t *read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(sz);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "Out of memory\n");
        return NULL;
    }
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        fprintf(stderr, "Read error\n");
        return NULL;
    }
    fclose(f);
    *size_out = sz;
    return buf;
}

/* Compare uint32_t for qsort */
static int cmp_u32(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: mkbin <input.elf> <output>\n");
        return 1;
    }

    /* Read ELF file */
    size_t elf_size;
    uint8_t *elf = read_file(argv[1], &elf_size);
    if (!elf)
        return 1;

    /* Validate ELF header */
    if (elf_size < 52 || memcmp(elf, ELFMAG, 4) != 0) {
        fprintf(stderr, "Not an ELF file\n");
        free(elf);
        return 1;
    }
    if (elf[4] != ELFCLASS32 || elf[5] != ELFDATA2MSB) {
        fprintf(stderr, "Not a 32-bit big-endian ELF\n");
        free(elf);
        return 1;
    }
    if (be16(elf + 18) != EM_68K) {
        fprintf(stderr, "Not an m68k ELF (machine=%d)\n", be16(elf + 18));
        free(elf);
        return 1;
    }

    /* Parse ELF header */
    uint32_t entry = be32(elf + 24);
    uint32_t phoff = be32(elf + 28);
    uint32_t shoff = be32(elf + 32);
    uint16_t phentsize = be16(elf + 42);
    uint16_t phnum = be16(elf + 44);
    uint16_t shentsize = be16(elf + 46);
    uint16_t shnum = be16(elf + 48);

    if (phnum == 0) {
        fprintf(stderr, "No program headers\n");
        free(elf);
        return 1;
    }

    /* Find PT_LOAD segments */
    uint32_t load_base = 0xFFFFFFFF;
    uint32_t load_end_file = 0;
    uint32_t load_end_mem = 0;

    for (int i = 0; i < phnum; i++) {
        uint8_t *ph = elf + phoff + i * phentsize;
        uint32_t p_type = be32(ph + 0);

        if (p_type != PT_LOAD)
            continue;

        uint32_t p_vaddr = be32(ph + 8);
        uint32_t p_filesz = be32(ph + 16);
        uint32_t p_memsz = be32(ph + 20);

        if (p_vaddr < load_base)
            load_base = p_vaddr;

        uint32_t end_file = p_vaddr + p_filesz;
        uint32_t end_mem = p_vaddr + p_memsz;

        if (end_file > load_end_file)
            load_end_file = end_file;
        if (end_mem > load_end_mem)
            load_end_mem = end_mem;
    }

    if (load_base == 0xFFFFFFFF) {
        fprintf(stderr, "No PT_LOAD segments\n");
        free(elf);
        return 1;
    }

    uint32_t load_size = load_end_file - load_base;
    uint32_t bss_size = load_end_mem - load_end_file;

    /* Determine text_size from section headers.
     * Text = all SHF_EXECINSTR sections (.text).
     * Data = everything in load_size after text ends. */
    uint32_t text_size = 0;
    uint32_t got_vma = 0;   /* VMA of .got section (0 = no GOT) */
    uint32_t got_size = 0;  /* size of .got section */
    if (shoff != 0 && shnum > 0) {
        /* Find section name string table */
        uint16_t shstrndx = be16(elf + 50);
        uint8_t *shstrtab = NULL;
        if (shstrndx < shnum) {
            uint8_t *sh_str = elf + shoff + shstrndx * shentsize;
            uint32_t str_off = be32(sh_str + 16);
            shstrtab = elf + str_off;
        }

        for (int i = 0; i < shnum; i++) {
            uint8_t *sh = elf + shoff + i * shentsize;
            uint32_t sh_name = be32(sh + 0);
            uint32_t sh_type = be32(sh + 4);
            uint32_t sh_flags = be32(sh + 8);
            uint32_t sh_addr = be32(sh + 12);
            uint32_t sh_size = be32(sh + 20);

            /* Count executable sections (.text) toward text_size */
            if (sh_type == SHT_PROGBITS && (sh_flags & SHF_EXECINSTR)) {
                uint32_t end = sh_addr + sh_size - load_base;
                if (end > text_size)
                    text_size = end;
            }

            /* Find .got section VMA for -msep-data GOT offset */
            if (shstrtab && sh_size > 0 &&
                strcmp((const char *)(shstrtab + sh_name), ".got") == 0) {
                got_vma = sh_addr;
                got_size = sh_size;
            }
        }
    }

    /* Compute GOT offset from data start (for -msep-data a5 setup).
     * Stored as (offset + 1) to distinguish "no GOT" (0) from
     * "GOT at data start" (1). The kernel subtracts 1 to get the
     * actual offset.
     * 0 = no GOT (binary not compiled with -msep-data)
     * N = GOT at offset (N-1) from start of data section */
    uint32_t got_offset = 0;
    if (got_vma > 0 && got_vma >= text_size)
        got_offset = (got_vma - text_size) + 1;

    /* Build flat binary from PT_LOAD segments */
    uint8_t *flat = calloc(1, load_size);
    if (!flat) {
        fprintf(stderr, "Out of memory\n");
        free(elf);
        return 1;
    }

    for (int i = 0; i < phnum; i++) {
        uint8_t *ph = elf + phoff + i * phentsize;
        uint32_t p_type = be32(ph + 0);

        if (p_type != PT_LOAD)
            continue;

        uint32_t p_offset = be32(ph + 4);
        uint32_t p_vaddr = be32(ph + 8);
        uint32_t p_filesz = be32(ph + 16);

        if (p_filesz > 0 && p_offset + p_filesz <= elf_size) {
            memcpy(flat + (p_vaddr - load_base), elf + p_offset, p_filesz);
        }
    }

    /* Extract R_68K_32 relocations from SHT_RELA sections.
     * These are absolute 32-bit address references that need
     * the load address added at runtime. */
    uint32_t reloc_cap = 256;
    uint32_t reloc_count = 0;
    uint32_t *relocs = malloc(reloc_cap * sizeof(uint32_t));
    if (!relocs) {
        fprintf(stderr, "Out of memory\n");
        free(flat);
        free(elf);
        return 1;
    }

    if (shoff != 0 && shnum > 0) {
        for (int i = 0; i < shnum; i++) {
            uint8_t *sh = elf + shoff + i * shentsize;
            uint32_t sh_type = be32(sh + 4);

            if (sh_type != SHT_RELA)
                continue;

            /* sh_info points to the section these relocs apply to.
             * Only process relocs for allocated (loaded) sections —
             * skip .rela.debug_* and other non-loaded sections. */
            uint32_t sh_info_idx = be32(sh + 28);
            if (sh_info_idx < shnum) {
                uint8_t *target_sh = elf + shoff + sh_info_idx * shentsize;
                uint32_t target_flags = be32(target_sh + 8);
                if (!(target_flags & SHF_ALLOC))
                    continue;  /* skip non-loaded sections (debug, etc.) */
            }

            uint32_t sh_offset = be32(sh + 16);
            uint32_t sh_size = be32(sh + 20);
            uint32_t sh_entsize = be32(sh + 36);

            if (sh_entsize < 12)
                continue;  /* invalid RELA entry size */

            uint32_t nentries = sh_size / sh_entsize;
            for (uint32_t j = 0; j < nentries; j++) {
                uint8_t *re = elf + sh_offset + j * sh_entsize;
                uint32_t r_offset = be32(re + 0);
                uint32_t r_info = be32(re + 4);
                uint32_t r_type = r_info & 0xFF;

                if (r_type == R_68K_32) {
                    /* Absolute 32-bit reference — needs relocation */
                    uint32_t off = r_offset - load_base;

                    /* Validate offset is within loaded image */
                    if (off + 4 > load_size) {
                        fprintf(stderr, "mkbin: reloc offset 0x%x out of range "
                                "(load_size=0x%x)\n", off, load_size);
                        free(relocs);
                        free(flat);
                        free(elf);
                        return 1;
                    }

                    /* 68000 bus-faults on word/long access at odd addresses */
                    if (off & 1) {
                        fprintf(stderr, "mkbin: ERROR: reloc offset 0x%x is "
                                "odd (would cause 68000 address error)\n", off);
                        free(relocs);
                        free(flat);
                        free(elf);
                        return 1;
                    }

                    /* Grow reloc array if needed */
                    if (reloc_count >= reloc_cap) {
                        reloc_cap *= 2;
                        relocs = realloc(relocs, reloc_cap * sizeof(uint32_t));
                        if (!relocs) {
                            fprintf(stderr, "Out of memory\n");
                            free(flat);
                            free(elf);
                            return 1;
                        }
                    }
                    relocs[reloc_count++] = off;
                } else if (r_type == R_68K_16) {
                    fprintf(stderr, "mkbin: ERROR: R_68K_16 relocation at "
                            "offset 0x%x — 16-bit absolute not supported\n",
                            r_offset);
                    free(relocs);
                    free(flat);
                    free(elf);
                    return 1;
                }
                /* R_68K_PC32/PC16/PC8/GOT16O: skip (PC-relative or
                 * GOT-offset, no runtime patching needed) */
            }
        }
    }

    /* Add synthetic relocations for GOT entries.
     * With -msep-data, the linker fills GOT entries with absolute addresses
     * but --emit-relocs does NOT emit R_68K_32 relocations for them.
     * We scan the .got section and add a relocation for each non-zero entry. */
    if (got_vma > 0 && got_size >= 4) {
        uint32_t got_file_off = got_vma - load_base;
        uint32_t got_entries = got_size / 4;
        for (uint32_t i = 0; i < got_entries; i++) {
            uint32_t off = got_file_off + i * 4;
            if (off + 4 > load_size)
                break;
            uint32_t val = be32(flat + off);
            if (val == 0)
                continue;  /* skip empty entries */
            /* Grow reloc array if needed */
            if (reloc_count >= reloc_cap) {
                reloc_cap *= 2;
                relocs = realloc(relocs, reloc_cap * sizeof(uint32_t));
                if (!relocs) {
                    fprintf(stderr, "Out of memory\n");
                    free(flat);
                    free(elf);
                    return 1;
                }
            }
            relocs[reloc_count++] = off;
        }
    }

    free(elf);

    /* Sort relocation offsets (cache-friendly access during relocation) */
    if (reloc_count > 1)
        qsort(relocs, reloc_count, sizeof(uint32_t), cmp_u32);

    /* Remove duplicates (can occur with merged sections) */
    if (reloc_count > 1) {
        uint32_t unique = 1;
        for (uint32_t i = 1; i < reloc_count; i++) {
            if (relocs[i] != relocs[unique - 1])
                relocs[unique++] = relocs[i];
        }
        reloc_count = unique;
    }

    printf("mkbin: base=0x%06x load=%u (text=%u data=%u) bss=%u "
           "entry=0x%06x relocs=%u",
           load_base, load_size, text_size, load_size - text_size,
           bss_size, entry, reloc_count);
    if (got_offset > 0)
        printf(" got_offset=%u", got_offset);
    printf("\n");

    /* Write output: 32-byte header + flat binary + relocation table */
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        free(relocs);
        free(flat);
        return 1;
    }

    /* Write big-endian header.
     * stack_size field packs two values:
     *   bits 0-15:  actual stack size (0 = default 4KB)
     *   bits 16-31: GOT offset from data start (for -msep-data a5 setup)
     * Programs without -msep-data have got_offset=0 (backward compatible). */
    uint32_t stack_field = 4096 | (got_offset << 16);

    uint8_t hdr[32];
    memset(hdr, 0, sizeof(hdr));
    put32(hdr + 0,  GENIX_MAGIC);
    put32(hdr + 4,  load_size);
    put32(hdr + 8,  bss_size);
    put32(hdr + 12, entry);
    put32(hdr + 16, stack_field);   /* stack size + GOT offset */
    put32(hdr + 20, 0);             /* flags: reserved */
    put32(hdr + 24, text_size);     /* text segment size */
    put32(hdr + 28, reloc_count);   /* relocation entry count */

    fwrite(hdr, 1, 32, out);
    fwrite(flat, 1, load_size, out);

    /* Write relocation table (big-endian uint32_t offsets) */
    for (uint32_t i = 0; i < reloc_count; i++) {
        uint8_t entry_be[4];
        put32(entry_be, relocs[i]);
        fwrite(entry_be, 1, 4, out);
    }

    fclose(out);

    uint32_t total = 32 + load_size + reloc_count * 4;
    printf("mkbin: wrote %u bytes (32 hdr + %u data + %u reloc)\n",
           total, load_size, reloc_count * 4);

    free(relocs);
    free(flat);
    return 0;
}
