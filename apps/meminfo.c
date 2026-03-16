/*
 * meminfo — display system memory layout and usage
 *
 * Shows kernel heap stats, per-process memory regions, and XIP text info.
 */
#include <stdio.h>
#include <stdint.h>

/* Must match kernel's struct region_info / struct meminfo exactly */
struct region_info {
    uint8_t  used;
    uint8_t  pid;
    uint16_t _pad;
    uint32_t base;
    uint32_t size;
    uint32_t text_size;
    uint32_t data_bss;
    uint32_t brk;
};

#define MAX_REGIONS 16

struct meminfo {
    uint32_t kheap_total;
    uint32_t kheap_free;
    uint32_t kheap_largest;
    uint32_t user_base;
    uint32_t user_top;
    uint32_t user_free;
    uint32_t user_largest;
    struct region_info regions[MAX_REGIONS];
};

extern int meminfo(struct meminfo *info);

int main(void)
{
    struct meminfo mi;

    if (meminfo(&mi) < 0) {
        puts("meminfo: syscall failed");
        return 1;
    }

    printf("=== Genix Memory Monitor ===\n\n");

    /* Kernel heap */
    unsigned int kused = mi.kheap_total - mi.kheap_free;
    printf("Kernel heap: %u / %u bytes used (%u free, largest %u)\n",
           kused, mi.kheap_total, mi.kheap_free, mi.kheap_largest);

    /* User memory overview */
    unsigned int user_total = mi.user_top - mi.user_base;
    printf("\nUser space:  0x%x - 0x%x  (%u bytes)\n",
           mi.user_base, mi.user_top, user_total);
    printf("Free:        %u bytes (largest %u)\n\n",
           mi.user_free, mi.user_largest);

    /* Per-process detail */
    for (int i = 0; i < MAX_REGIONS; i++) {
        struct region_info *r = &mi.regions[i];
        if (!r->used)
            continue;
        printf("PID %d: 0x%x  (%u bytes)\n",
               r->pid, r->base, r->size);
        if (r->text_size > 0)
            printf("  ROM text (XIP): %u bytes\n", r->text_size);
        printf("  Data+BSS:       %u / %u bytes\n", r->data_bss, r->size);
        unsigned int heap_used = 0;
        if (r->brk > r->base + r->data_bss)
            heap_used = r->brk - (r->base + r->data_bss);
        printf("  Heap:           %u bytes\n", heap_used);
        unsigned int stack_reserve = 4096;
        unsigned int used = r->data_bss + heap_used;
        unsigned int avail = r->size > used + stack_reserve ?
                             r->size - used - stack_reserve : 0;
        printf("  Stack reserve:  %u bytes\n", stack_reserve);
        printf("  Free in region: %u bytes\n", avail);
    }

    return 0;
}
