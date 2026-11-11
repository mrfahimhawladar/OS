#ifndef FAHIM_OS_MEMORY_H
#define FAHIM_OS_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define MEMORY_MAP_MAX_REGIONS 8u

typedef struct {
    uint64_t base;
    uint64_t size;
} memory_region_t;

/*
 * Read enabled FDT nodes whose device_type is "memory".
 * Returns 0 on success, -1 for an invalid FDT, -2 for malformed/unsupported
 * memory data, -3 when no usable RAM regions are present, and -4 when the
 * caller-provided region array is too small.
 */
int fdt_memory_map_from_dtb(const void *dtb,
                            memory_region_t *regions,
                            size_t capacity,
                            size_t *region_count);

#endif
