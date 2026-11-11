#ifndef FAHIM_OS_PAGE_ALLOCATOR_H
#define FAHIM_OS_PAGE_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include "memory.h"

/* Returns 0 on success; input ranges are the validated memory_layout.usable array. */
int32_t fahim_page_allocator_init(const memory_region_t *regions,
                                  size_t region_count);

/* Allocate count contiguous 4 KiB pages; UINT64_MAX means allocation failure. */
uint64_t fahim_page_allocator_alloc(size_t count);

/*
 * Free an exact live allocation returned by fahim_page_allocator_alloc.
 * The caller must no longer access any page in the allocation.
 */
int32_t fahim_page_allocator_free(uint64_t address, size_t count);

uint64_t fahim_page_allocator_total_bytes(void);
uint64_t fahim_page_allocator_free_bytes(void);
int32_t fahim_page_allocator_self_test(void);

#endif
