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
    uint32_t kused = mi.kheap_total - mi.kheap_free;
    printf("Kernel heap: %lu / %lu bytes used (%lu free, largest %lu)\n",
           (unsigned long)kused, (unsigned long)mi.kheap_total,
           (unsigned long)mi.kheap_free, (unsigned long)mi.kheap_largest);

    /* User memory overview */
    uint32_t user_total = mi.user_top - mi.user_base;
    printf("\nUser space:  0x%lx - 0x%lx  (%lu bytes)\n",
           (unsigned long)mi.user_base, (unsigned long)mi.user_top,
           (unsigned long)user_total);
    printf("Free:        %lu bytes (largest %lu)\n\n",
           (unsigned long)mi.user_free, (unsigned long)mi.user_largest);

    /* Per-process detail */
    for (int i = 0; i < MAX_REGIONS; i++) {
        struct region_info *r = &mi.regions[i];
        if (!r->used)
            continue;
        printf("PID %d: 0x%lx  (%lu bytes)\n",
               r->pid, (unsigned long)r->base, (unsigned long)r->size);
        if (r->text_size > 0)
            printf("  ROM text (XIP): %lu bytes\n",
                   (unsigned long)r->text_size);
        printf("  Data+BSS:       %lu / %lu bytes\n",
               (unsigned long)r->data_bss, (unsigned long)r->size);
        uint32_t heap_used = 0;
        if (r->brk > r->base + r->data_bss)
            heap_used = r->brk - (r->base + r->data_bss);
        printf("  Heap:           %lu bytes\n", (unsigned long)heap_used);
        uint32_t stack_reserve = 4096;
        uint32_t used = r->data_bss + heap_used;
        uint32_t avail = r->size > used + stack_reserve ?
                         r->size - used - stack_reserve : 0;
        printf("  Stack reserve:  %lu bytes\n", (unsigned long)stack_reserve);
        printf("  Free in region: %lu bytes\n", (unsigned long)avail);
    }

    return 0;
}
