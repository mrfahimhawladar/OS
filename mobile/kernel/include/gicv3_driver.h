#ifndef FAHIM_OS_GICV3_DRIVER_H
#define FAHIM_OS_GICV3_DRIVER_H

#include <stddef.h>
#include <stdint.h>

int32_t fahim_gicv3_init(uint64_t gicd_base,
                         uint64_t gicr_base,
                         size_t cpu_count);
uint32_t fahim_gicv3_read_priority_mask(void);
uint32_t fahim_gicv3_dispatch_irq(void);

#endif
