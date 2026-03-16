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
    unsigned int kused = mi.kheap_total - mi.kheap_free;
    printf("Kernel heap: %u / %u bytes used (%u free, largest %u)\n",
           kused, mi.kheap_total, mi.kheap_free, mi.kheap_largest);

    /* User memory overview */
    unsigned int user_total = mi.user_top - mi.user_base;
    printf("\nUser space:  0x%x - 0x%x  (%u bytes)\n",
           mi.user_base, mi.user_top, user_total);
    printf("Slots:       %d x %u bytes\n\n",
           mi.num_slots, mi.slot_size);

    /* Per-slot detail */
    for (int i = 0; i < mi.num_slots; i++) {
        struct slot_info *s = &mi.slots[i];
        if (!s->used) {
            printf("Slot %d: [FREE]  0x%x\n", i, s->base);
            continue;
        }
        printf("Slot %d: [PID %d] 0x%x\n", i, s->pid, s->base);
        if (s->text_size > 0)
            printf("  ROM text (XIP): %u bytes\n", s->text_size);
        printf("  Data+BSS:       %u / %u bytes\n", s->data_bss, s->size);
        unsigned int heap_used = 0;
        if (s->brk > s->base + s->data_bss)
            heap_used = s->brk - (s->base + s->data_bss);
        printf("  Heap:           %u bytes\n", heap_used);
        unsigned int stack_reserve = 4096;
        unsigned int used = s->data_bss + heap_used;
        unsigned int avail = s->size > used + stack_reserve ?
                             s->size - used - stack_reserve : 0;
        printf("  Stack reserve:  %u bytes\n", stack_reserve);
        printf("  Free in slot:   %u bytes\n", avail);
    }

    return 0;
}
