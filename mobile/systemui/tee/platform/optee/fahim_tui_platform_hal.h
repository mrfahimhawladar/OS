/*
 * Fahim Trusted UI platform HAL contract.
 *
 * This header is compiled only into the OP-TEE core pseudo-TA overlay.
 * Board integrations provide strong implementations of these hooks using
 * secure-world-owned display and input drivers. The weak defaults in
 * fahim_tui_pta.c return TEE_ERROR_NOT_SUPPORTED.
 *
 * NEVER implement these hooks by reading PIN bytes from REE/shared-memory
 * input, an Android/Linux UI, a normal-world callback, or a REE framebuffer.
 */
#ifndef FAHIM_TUI_PLATFORM_HAL_H
#define FAHIM_TUI_PLATFORM_HAL_H

#include <tee_api_types.h>
#include <stddef.h>
#include <stdint.h>

TEE_Result fahim_tui_platform_begin(void);
TEE_Result fahim_tui_platform_read_pin(uint32_t prompt,
                                       uint8_t *secure_pin_buffer,
                                       size_t capacity,
                                       size_t *actual_length);
TEE_Result fahim_tui_platform_show_status(uint32_t status);
TEE_Result fahim_tui_platform_end(void);

#endif
