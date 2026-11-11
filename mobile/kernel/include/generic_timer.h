#ifndef FAHIM_OS_GENERIC_TIMER_H
#define FAHIM_OS_GENERIC_TIMER_H

#include <stdint.h>

int32_t fahim_generic_timer_init(uint32_t interval_ms);
void fahim_generic_timer_stop(void);
uint32_t fahim_generic_timer_frequency_hz(void);
uint32_t fahim_generic_timer_irq_count(void);

#endif
