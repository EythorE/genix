/*
 * meminfo — display system memory layout and usage
 *
 * Shows kernel heap stats, per-slot usage, and XIP text info.
 */
#include <stdio.h>
#include <stdint.h>

/* Must match kernel's struct slot_info / struct meminfo exactly */
struct slot_info {
    uint8_t  used;
    uint8_t  pid;
    uint16_t _pad;
    uint32_t base;
    uint32_t size;
    uint32_t text_size;
    uint32_t data_bss;
    uint32_t brk;
};

#define MAX_SLOTS 8

struct meminfo {
    uint32_t kheap_total;
    uint32_t kheap_free;
    uint32_t kheap_largest;
    uint32_t user_base;
    uint32_t user_top;
    uint32_t slot_size;
    uint8_t  num_slots;
    uint8_t  _pad[3];
    struct slot_info slots[MAX_SLOTS];
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
    printf("Slots:       %d x %lu bytes\n\n",
           mi.num_slots, (unsigned long)mi.slot_size);

    /* Per-slot detail */
    for (int i = 0; i < mi.num_slots; i++) {
        struct slot_info *s = &mi.slots[i];
        printf("Slot %d: ", i);
        if (!s->used) {
            printf("[FREE]  0x%lx\n", (unsigned long)s->base);
            continue;
        }
        printf("[PID %d] 0x%lx\n", s->pid, (unsigned long)s->base);
        if (s->text_size > 0)
            printf("  ROM text (XIP): %lu bytes\n",
                   (unsigned long)s->text_size);
        printf("  Data+BSS:       %lu / %lu bytes\n",
               (unsigned long)s->data_bss, (unsigned long)s->size);
        uint32_t heap_used = 0;
        if (s->brk > s->base + s->data_bss)
            heap_used = s->brk - (s->base + s->data_bss);
        printf("  Heap:           %lu bytes\n", (unsigned long)heap_used);
        uint32_t stack_reserve = 4096;
        uint32_t used = s->data_bss + heap_used;
        uint32_t avail = s->size > used + stack_reserve ?
                         s->size - used - stack_reserve : 0;
        printf("  Stack reserve:  %lu bytes\n", (unsigned long)stack_reserve);
        printf("  Free in slot:   %lu bytes\n", (unsigned long)avail);
    }

    return 0;
}
