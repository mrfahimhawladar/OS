#ifndef FAHIM_OS_GICV3_PLATFORM_H
#define FAHIM_OS_GICV3_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"

#define GICV3_MAX_REDISTRIBUTOR_REGIONS 4u

typedef struct {
    memory_region_t distributor;
    memory_region_t redistributors[GICV3_MAX_REDISTRIBUTOR_REGIONS];
    size_t redistributor_region_count;
    uint64_t redistributor_stride;
} gicv3_platform_info_t;

/*
 * Discover GICv3 register regions from the boot Device Tree.
 * This routine only discovers resources; it does not initialize the GIC.
 *
 * Returns:
 *   0 success; -1 invalid header/bounds; -2 malformed data;
 *   -3 no enabled GICv3 node; -4 ambiguous/inconsistent description;
 *   -5 too many redistributor regions for this early boot interface.
 */
int gicv3_discover_from_fdt(const void *dtb,
                            gicv3_platform_info_t *info);

#endif
