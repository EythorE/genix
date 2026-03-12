/*
 * Unit tests for relocatable binary support
 *
 * Tests the relocation engine (apply_relocations) and header validation
 * for the new relocatable binary format. Runs on the host (no 68000 needed).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "testutil.h"

/* Re-define kernel constants for host testing */
#define USER_BASE   0x040000
#define USER_TOP    0x0F0000
#define USER_SIZE   (USER_TOP - USER_BASE)
#define USER_STACK_DEFAULT 4096
#define GENIX_MAGIC 0x47454E58
#define GENIX_HDR_SIZE 32
#define ENOEXEC     8
#define ENOMEM      12

struct genix_header {
    uint32_t magic;
    uint32_t load_size;
    uint32_t bss_size;
    uint32_t entry;
    uint32_t stack_size;
    uint32_t flags;
    uint32_t text_size;
    uint32_t reloc_count;
};

/* Re-implement validation from exec.c for host testing */
static int exec_validate_header(const struct genix_header *hdr)
{
    if (hdr->magic != GENIX_MAGIC)
        return -ENOEXEC;

    if (hdr->load_size == 0)
        return -ENOEXEC;

    /* Entry must be within loaded region (0-based offset) */
    if (hdr->entry >= hdr->load_size)
        return -ENOEXEC;

    /* text_size must not exceed load_size */
    if (hdr->text_size > hdr->load_size)
        return -ENOEXEC;

    uint32_t stack = hdr->stack_size ? hdr->stack_size : USER_STACK_DEFAULT;
    uint32_t reloc_bytes = hdr->reloc_count * 4;
    uint32_t effective_bss = hdr->bss_size;
    if (reloc_bytes > effective_bss)
        effective_bss = reloc_bytes;

    uint32_t total = hdr->load_size + effective_bss + stack;
    if (total > USER_SIZE)
        return -ENOMEM;

    return 0;
}

/* Re-implement relocator from exec.c for host testing */
static void apply_relocations(uint8_t *base, uint32_t load_addr,
                               uint32_t text_size, uint32_t load_size,
                               const uint32_t *relocs, uint32_t nrelocs)
{
    (void)load_size;

    if (text_size == 0) {
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            uint32_t *ptr = (uint32_t *)(base + off);
            *ptr += load_addr;
        }
    } else {
        uint32_t text_base = load_addr;
        uint32_t data_base = load_addr + text_size;
        for (uint32_t i = 0; i < nrelocs; i++) {
            uint32_t off = relocs[i];
            uint32_t *ptr = (uint32_t *)(base + off);
            uint32_t val = *ptr;
            if (val < text_size)
                *ptr = val + text_base;
            else
                *ptr = (val - text_size) + data_base;
        }
    }
}

/* --- Header validation tests --- */

static void test_header_valid_reloc(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 256,
        .entry = 0,        /* 0-based offset */
        .stack_size = 4096,
        .flags = 0,
        .text_size = 512,
        .reloc_count = 10,
    };
    ASSERT_EQ(exec_validate_header(&hdr), 0);
}

static void test_header_entry_zero(void)
{
    /* Entry at offset 0 is valid (typical _start) */
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 256,
        .entry = 0,
        .stack_size = 4096,
    };
    ASSERT_EQ(exec_validate_header(&hdr), 0);
}

static void test_header_entry_past_load(void)
{
    /* Entry at load_size is past the loaded region */
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 0,
        .entry = 1024,
    };
    ASSERT_EQ(exec_validate_header(&hdr), -ENOEXEC);
}

static void test_header_entry_within_load(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 0,
        .entry = 512,
    };
    ASSERT_EQ(exec_validate_header(&hdr), 0);
}

static void test_header_bad_magic(void)
{
    struct genix_header hdr = {
        .magic = 0xDEADBEEF,
        .load_size = 1024,
        .entry = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr), -ENOEXEC);
}

static void test_header_zero_load(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 0,
        .entry = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr), -ENOEXEC);
}

static void test_header_too_large(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = USER_SIZE + 1,
        .entry = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr), -ENOMEM);
}

static void test_header_text_exceeds_load(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 256,
        .entry = 0,
        .text_size = 2048,  /* > load_size */
    };
    ASSERT_EQ(exec_validate_header(&hdr), -ENOEXEC);
}

static void test_header_reloc_extends_bss(void)
{
    /* Relocation table larger than BSS — effective_bss grows */
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 4,       /* tiny BSS */
        .entry = 0,
        .stack_size = 4096,
        .reloc_count = 100,  /* 400 bytes of reloc > 4 bytes BSS */
    };
    /* total = 1024 + 400 + 4096 = 5520, well within USER_SIZE */
    ASSERT_EQ(exec_validate_header(&hdr), 0);
}

static void test_header_size(void)
{
    ASSERT_EQ(sizeof(struct genix_header), 32);
}

static void test_header_no_relocs(void)
{
    struct genix_header hdr = {
        .magic = GENIX_MAGIC,
        .load_size = 1024,
        .bss_size = 256,
        .entry = 0,
        .reloc_count = 0,
    };
    ASSERT_EQ(exec_validate_header(&hdr), 0);
}

/* --- Relocation tests --- */

/* Helper: write a big-endian uint32_t (matching 68000 byte order) */
static void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* Helper: read a big-endian uint32_t */
static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/*
 * NOTE: On the host (likely little-endian x86), the relocator works
 * with native uint32_t values. On the 68000 (big-endian), the flat
 * binary is already in native byte order. For host testing, we work
 * with native uint32_t — the relocator adds to uint32_t regardless
 * of endianness.
 */

static uint8_t test_image[4096] __attribute__((aligned(4)));

static void test_reloc_simple_single(void)
{
    /* Single relocation: value 0x100 at offset 4 */
    memset(test_image, 0, sizeof(test_image));
    uint32_t *p = (uint32_t *)(test_image + 4);
    *p = 0x100;

    uint32_t relocs[] = { 4 };
    apply_relocations(test_image, 0x040000, 0, 1024, relocs, 1);

    ASSERT_EQ(*p, 0x040100);
}

static void test_reloc_simple_multiple(void)
{
    /* Multiple relocations */
    memset(test_image, 0, sizeof(test_image));
    uint32_t *p0 = (uint32_t *)(test_image + 0);
    uint32_t *p4 = (uint32_t *)(test_image + 8);
    uint32_t *p8 = (uint32_t *)(test_image + 16);
    *p0 = 0x000;
    *p4 = 0x100;
    *p8 = 0x200;

    uint32_t relocs[] = { 0, 8, 16 };
    apply_relocations(test_image, 0xFF9000, 0, 1024, relocs, 3);

    ASSERT_EQ(*p0, 0xFF9000);
    ASSERT_EQ(*p4, 0xFF9100);
    ASSERT_EQ(*p8, 0xFF9200);
}

static void test_reloc_zero_relocs(void)
{
    /* Zero relocation entries — image unchanged */
    memset(test_image, 0, sizeof(test_image));
    uint32_t *p = (uint32_t *)test_image;
    *p = 0x12345678;

    apply_relocations(test_image, 0x040000, 0, 1024, NULL, 0);

    ASSERT_EQ(*p, 0x12345678);
}

static void test_reloc_edge_last_offset(void)
{
    /* Reloc at the last valid offset (load_size - 4) */
    memset(test_image, 0, 1024);
    uint32_t *p = (uint32_t *)(test_image + 1020);
    *p = 0x50;

    uint32_t relocs[] = { 1020 };
    apply_relocations(test_image, 0x040000, 0, 1024, relocs, 1);

    ASSERT_EQ(*p, 0x040050);
}

static void test_reloc_split_text_ref(void)
{
    /* Split reloc: value < text_size -> references text */
    memset(test_image, 0, sizeof(test_image));

    uint32_t text_size = 512;
    /* A word in data section (offset 600) referencing code (value 0x100) */
    uint32_t *p = (uint32_t *)(test_image + 600);
    *p = 0x100;  /* < text_size, so it's a text reference */

    uint32_t relocs[] = { 600 };
    apply_relocations(test_image, 0x040000, text_size, 1024, relocs, 1);

    /* text_base = 0x040000, value + text_base = 0x040100 */
    ASSERT_EQ(*p, 0x040100);
}

static void test_reloc_split_data_ref(void)
{
    /* Split reloc: value >= text_size -> references data */
    memset(test_image, 0, sizeof(test_image));

    uint32_t text_size = 512;
    /* A word in text section (offset 100) referencing data (value 0x204) */
    uint32_t *p = (uint32_t *)(test_image + 100);
    *p = 0x204;  /* >= text_size (512), so it's a data reference */

    uint32_t relocs[] = { 100 };
    apply_relocations(test_image, 0x040000, text_size, 1024, relocs, 1);

    /* data_base = 0x040000 + 512 = 0x040200
     * result = (0x204 - 512) + 0x040200 = 0x04 + 0x040200 = 0x040204 */
    ASSERT_EQ(*p, 0x040204);
}

static void test_reloc_split_boundary(void)
{
    /* Split reloc: value == text_size -> first byte of data */
    memset(test_image, 0, sizeof(test_image));

    uint32_t text_size = 512;
    uint32_t *p = (uint32_t *)(test_image + 200);
    *p = text_size;  /* == text_size, so it's data reference (offset 0 in data) */

    uint32_t relocs[] = { 200 };
    apply_relocations(test_image, 0x040000, text_size, 1024, relocs, 1);

    /* data_base = 0x040200, offset in data = 0, result = 0x040200 */
    ASSERT_EQ(*p, 0x040200);
}

static void test_reloc_split_mixed(void)
{
    /* Mixed text and data references */
    memset(test_image, 0, sizeof(test_image));

    uint32_t text_size = 256;
    /* Offset 0: text ref (value 0x10 < 256) */
    uint32_t *p0 = (uint32_t *)(test_image + 0);
    *p0 = 0x10;
    /* Offset 8: data ref (value 0x110 >= 256) */
    uint32_t *p1 = (uint32_t *)(test_image + 8);
    *p1 = 0x110;
    /* Offset 300: text ref from data area (value 0x20 < 256) */
    uint32_t *p2 = (uint32_t *)(test_image + 300);
    *p2 = 0x20;

    uint32_t relocs[] = { 0, 8, 300 };
    apply_relocations(test_image, 0xFF9000, text_size, 1024, relocs, 3);

    /* text_base = 0xFF9000, data_base = 0xFF9100 */
    ASSERT_EQ(*p0, 0xFF9010);   /* 0x10 + 0xFF9000 */
    ASSERT_EQ(*p1, 0xFF9110);   /* (0x110 - 256) + 0xFF9100 = 0x10 + 0xFF9100 */
    ASSERT_EQ(*p2, 0xFF9020);   /* 0x20 + 0xFF9000 */
}

static void test_reloc_contiguous_split_same_as_simple(void)
{
    /* When text and data are contiguous, split reloc should give
     * the same result as simple reloc (just adding load_addr) */
    uint8_t img_simple[1024];
    uint8_t img_split[1024];
    memset(img_simple, 0, sizeof(img_simple));
    memset(img_split, 0, sizeof(img_split));

    uint32_t text_size = 512;
    /* Set up identical images with a value in data area */
    uint32_t *ps = (uint32_t *)(img_simple + 600);
    uint32_t *pp = (uint32_t *)(img_split + 600);
    *ps = 0x300;  /* This is >= text_size, so it's a data ref */
    *pp = 0x300;

    /* Also a text ref */
    uint32_t *ps2 = (uint32_t *)(img_simple + 0);
    uint32_t *pp2 = (uint32_t *)(img_split + 0);
    *ps2 = 0x100;  /* < text_size, text ref */
    *pp2 = 0x100;

    uint32_t relocs[] = { 0, 600 };
    apply_relocations(img_simple, 0x040000, 0, 1024, relocs, 2);
    apply_relocations(img_split, 0x040000, text_size, 1024, relocs, 2);

    /* For contiguous load, both should produce the same result */
    ASSERT_EQ(*ps, *pp);
    ASSERT_EQ(*ps2, *pp2);
}

static void test_reloc_md_address(void)
{
    /* Test with Mega Drive USER_BASE (0xFF9000) */
    memset(test_image, 0, sizeof(test_image));
    uint32_t *p = (uint32_t *)(test_image + 16);
    *p = 0x28;  /* entry point of main */

    uint32_t relocs[] = { 16 };
    apply_relocations(test_image, 0xFF9000, 0, 1024, relocs, 1);

    ASSERT_EQ(*p, 0xFF9028);
}

/* --- Dynamic load address tests (Phase 6) --- */

static void test_reloc_dynamic_same_binary_two_addresses(void)
{
    /* Same binary image relocated to two different addresses must
     * produce the correct relocated values at each address. This is
     * the core property that enables multitasking: one binary, two
     * processes at different load addresses. */
    uint8_t img_a[1024], img_b[1024];
    memset(img_a, 0, sizeof(img_a));
    memset(img_b, 0, sizeof(img_b));

    /* Set up identical zero-based images */
    uint32_t *pa = (uint32_t *)(img_a + 0);
    uint32_t *pb = (uint32_t *)(img_b + 0);
    *pa = 0x100;   /* reference to address 0x100 in the binary */
    *pb = 0x100;

    uint32_t *pa2 = (uint32_t *)(img_a + 8);
    uint32_t *pb2 = (uint32_t *)(img_b + 8);
    *pa2 = 0;      /* reference to start of binary */
    *pb2 = 0;

    uint32_t relocs[] = { 0, 8 };

    /* Relocate to two different addresses */
    uint32_t addr_a = 0x040000;   /* workbench */
    uint32_t addr_b = 0x041000;   /* hypothetical second process */
    apply_relocations(img_a, addr_a, 0, 1024, relocs, 2);
    apply_relocations(img_b, addr_b, 0, 1024, relocs, 2);

    ASSERT_EQ(*pa, 0x040100);
    ASSERT_EQ(*pa2, 0x040000);
    ASSERT_EQ(*pb, 0x041100);
    ASSERT_EQ(*pb2, 0x041000);
}

static void test_reloc_dynamic_offset_entry(void)
{
    /* Entry point computation: entry is 0-based offset, absolute
     * entry = load_addr + entry. Different load addresses produce
     * different absolute entries. */
    uint32_t entry_offset = 0x20;
    uint32_t abs_a = 0x040000 + entry_offset;
    uint32_t abs_b = 0xFF9000 + entry_offset;
    uint32_t abs_c = 0x041000 + entry_offset;

    ASSERT_EQ(abs_a, 0x040020);
    ASSERT_EQ(abs_b, 0xFF9020);
    ASSERT_EQ(abs_c, 0x041020);
}

static void test_reloc_dynamic_arbitrary_address(void)
{
    /* Relocate to an arbitrary non-standard address (not USER_BASE).
     * This simulates loading a second process at a different location. */
    memset(test_image, 0, sizeof(test_image));
    uint32_t *p0 = (uint32_t *)(test_image + 0);
    uint32_t *p4 = (uint32_t *)(test_image + 4);
    *p0 = 0x10;    /* text reference */
    *p4 = 0x200;   /* data reference */

    uint32_t relocs[] = { 0, 4 };
    uint32_t load_addr = 0x050000;  /* arbitrary non-standard address */
    apply_relocations(test_image, load_addr, 0, 1024, relocs, 2);

    ASSERT_EQ(*p0, 0x050010);
    ASSERT_EQ(*p4, 0x050200);
}

static void test_reloc_dynamic_split_at_arbitrary(void)
{
    /* Split-mode relocation at a non-standard address */
    memset(test_image, 0, sizeof(test_image));
    uint32_t text_size = 256;
    uint32_t *p0 = (uint32_t *)(test_image + 0);
    uint32_t *p4 = (uint32_t *)(test_image + 4);
    *p0 = 0x10;     /* < text_size -> text ref */
    *p4 = 0x110;    /* >= text_size -> data ref */

    uint32_t relocs[] = { 0, 4 };
    uint32_t load_addr = 0x060000;
    apply_relocations(test_image, load_addr, text_size, 1024, relocs, 2);

    /* text_base = 0x060000, data_base = 0x060100 */
    ASSERT_EQ(*p0, 0x060010);    /* 0x10 + 0x060000 */
    ASSERT_EQ(*p4, 0x060110);    /* (0x110 - 256) + 0x060100 = 0x10 + 0x060100 */
}

/* --- Main --- */

int main(void)
{
    printf("test_reloc:\n");

    /* Header validation */
    RUN_TEST(test_header_valid_reloc);
    RUN_TEST(test_header_entry_zero);
    RUN_TEST(test_header_entry_past_load);
    RUN_TEST(test_header_entry_within_load);
    RUN_TEST(test_header_bad_magic);
    RUN_TEST(test_header_zero_load);
    RUN_TEST(test_header_too_large);
    RUN_TEST(test_header_text_exceeds_load);
    RUN_TEST(test_header_reloc_extends_bss);
    RUN_TEST(test_header_size);
    RUN_TEST(test_header_no_relocs);

    /* Relocation engine */
    RUN_TEST(test_reloc_simple_single);
    RUN_TEST(test_reloc_simple_multiple);
    RUN_TEST(test_reloc_zero_relocs);
    RUN_TEST(test_reloc_edge_last_offset);
    RUN_TEST(test_reloc_split_text_ref);
    RUN_TEST(test_reloc_split_data_ref);
    RUN_TEST(test_reloc_split_boundary);
    RUN_TEST(test_reloc_split_mixed);
    RUN_TEST(test_reloc_contiguous_split_same_as_simple);
    RUN_TEST(test_reloc_md_address);

    /* Dynamic load address (Phase 6) */
    RUN_TEST(test_reloc_dynamic_same_binary_two_addresses);
    RUN_TEST(test_reloc_dynamic_offset_entry);
    RUN_TEST(test_reloc_dynamic_arbitrary_address);
    RUN_TEST(test_reloc_dynamic_split_at_arbitrary);

    TEST_REPORT();
}
