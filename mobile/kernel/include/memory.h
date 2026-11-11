#ifndef FAHIM_OS_MEMORY_H
#define FAHIM_OS_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define MEMORY_MAP_MAX_RAM_REGIONS      8u
#define MEMORY_MAP_MAX_RESERVED_REGIONS 32u
#define MEMORY_MAP_MAX_USABLE_REGIONS   \
    (MEMORY_MAP_MAX_RAM_REGIONS + MEMORY_MAP_MAX_RESERVED_REGIONS)

typedef struct {
    uint64_t base;
    uint64_t size;
} memory_region_t;

typedef struct {
    memory_region_t ram[MEMORY_MAP_MAX_RAM_REGIONS];
    size_t ram_count;

    memory_region_t reserved[MEMORY_MAP_MAX_RESERVED_REGIONS];
    size_t reserved_count;

    memory_region_t usable[MEMORY_MAP_MAX_USABLE_REGIONS];
    size_t usable_count;
} memory_layout_t;

/*
 * Build a diagnostic memory layout from the boot Device Tree.
 * Reservations include the FDT reserve map, /reserved-memory children,
 * the kernel image, and the DTB itself. Usable ranges are RAM minus
 * all merged reservations.
 *
 * Returns: 0 success; -1 invalid input/header; -2 malformed FDT or overflow;
 * -3 no usable RAM; -4 capacity exceeded; -5 dynamic reserved-memory
 * allocation is not yet supported.
 */
int memory_layout_from_dtb(const void *dtb,
                           uintptr_t kernel_start,
                           uintptr_t kernel_end,
                           memory_layout_t *layout);

#endif
