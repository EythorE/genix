/*
 * mkbin — Convert m68k ELF to Genix flat binary
 *
 * Reads an ELF executable, extracts loadable segments,
 * and produces a Genix binary with a 32-byte header.
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
    uint32_t load_size;
    uint32_t bss_size;
    uint32_t entry;
    uint32_t stack_size;
    uint32_t flags;
    uint32_t reserved[2];
};

/* Minimal ELF structures for 32-bit big-endian */
#define EI_NIDENT   16
#define ELFMAG      "\177ELF"
#define ELFCLASS32  1
#define ELFDATA2MSB 2
#define EM_68K      4
#define PT_LOAD     1

struct elf32_hdr {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

/* Big-endian read helpers (ELF for 68000 is big-endian) */
static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Big-endian write helpers (output is big-endian for 68000) */
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

    /* Parse ELF header fields */
    uint32_t entry = be32(elf + 24);
    uint32_t phoff = be32(elf + 28);
    uint16_t phentsize = be16(elf + 42);
    uint16_t phnum = be16(elf + 44);

    if (phnum == 0) {
        fprintf(stderr, "No program headers\n");
        free(elf);
        return 1;
    }

    /* Find PT_LOAD segments */
    uint32_t load_base = 0xFFFFFFFF;
    uint32_t load_end_file = 0;   /* end of file-backed data (vaddr) */
    uint32_t load_end_mem = 0;    /* end of memory (vaddr, includes BSS) */

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

    printf("mkbin: base=0x%06x load=%u bss=%u entry=0x%06x\n",
           load_base, load_size, bss_size, entry);

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

    free(elf);

    /* Write output: 32-byte header + flat binary */
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        free(flat);
        return 1;
    }

    /* Write big-endian header */
    uint8_t hdr[32];
    memset(hdr, 0, sizeof(hdr));
    put32(hdr + 0,  GENIX_MAGIC);
    put32(hdr + 4,  load_size);
    put32(hdr + 8,  bss_size);
    put32(hdr + 12, entry);
    put32(hdr + 16, 4096);     /* default stack size */
    put32(hdr + 20, 0);        /* flags */
    put32(hdr + 24, 0);        /* reserved */
    put32(hdr + 28, 0);        /* reserved */

    fwrite(hdr, 1, 32, out);
    fwrite(flat, 1, load_size, out);
    fclose(out);
    free(flat);

    printf("mkbin: wrote %u bytes (32 hdr + %u data)\n",
           32 + load_size, load_size);
    return 0;
}
