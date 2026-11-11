#ifndef FAHIM_OS_MMU_H
#define FAHIM_OS_MMU_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

/*
 * Build and activate an EL1 identity map using RAM and MMIO regions discovered
 * from Device Tree. The kernel text and rodata ranges must be page-aligned.
 */
int32_t fahim_mmu_init(const memory_region_t *ram_regions,
                       size_t ram_count,
                       const memory_region_t *device_regions,
                       size_t device_count,
                       uint64_t text_start,
                       uint64_t text_end,
                       uint64_t rodata_start,
                       uint64_t rodata_end);

uint32_t fahim_mmu_is_enabled(void);

#endif
