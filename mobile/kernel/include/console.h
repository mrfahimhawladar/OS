#ifndef FAHIM_OS_CONSOLE_H
#define FAHIM_OS_CONSOLE_H

#include <stdint.h>

int console_init_from_fdt(const void *dtb);
uint64_t console_uart_physical_base(void);
void console_putc(char character);
void console_write(const char *text);

#endif
