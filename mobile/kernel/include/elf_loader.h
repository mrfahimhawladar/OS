#ifndef FAHIM_OS_ELF_LOADER_H
#define FAHIM_OS_ELF_LOADER_H

#include <stddef.h>
#include <stdint.h>

#define FAHIM_ELF64_MAX_SEGMENTS 8u

typedef struct {
    uint64_t virtual_base;
    uint64_t physical_base;
    uint32_t page_count;
    uint32_t permissions;
} fahim_elf_loaded_segment_t;

typedef struct {
    uint64_t entry;
    uint32_t address_space;
    uint32_t segment_count;
    fahim_elf_loaded_segment_t segments[FAHIM_ELF64_MAX_SEGMENTS];
} fahim_elf_loaded_image_t;

/*
 * Load a little-endian AArch64 ET_EXEC image into an inactive address space.
 * Supports bounded PT_LOAD-only static executables. Dynamic linking, PT_TLS,
 * PIE/ET_DYN and W+X mappings are rejected.
 */
int32_t fahim_elf64_load_user_image(uint32_t address_space,
                                    const uint8_t *image,
                                    size_t image_length,
                                    fahim_elf_loaded_image_t *output);

/*
 * Release the physical segment allocations after the associated address
 * space has been destroyed. Do not modify the returned image structure.
 */
int32_t fahim_elf64_release_user_image(fahim_elf_loaded_image_t *image);

#endif
