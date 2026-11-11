#ifndef FAHIM_OS_ADDRESS_SPACE_H
#define FAHIM_OS_ADDRESS_SPACE_H

#include <stdint.h>

/*
 * Early EL0 address-space APIs. Handles are generation-tagged so a stale
 * handle cannot refer to a later process after a slot is reused.
 *
 * User physical pages must be allocated by the kernel page allocator and
 * remain owned by the caller; destroying an address space frees page-table
 * frames, not the mapped user-data frames.
 */
#define FAHIM_USER_PAGE_RW_XN 0u
#define FAHIM_USER_PAGE_RO_X  1u

uint32_t fahim_address_space_create(void);
uint32_t fahim_address_space_asid(uint32_t handle);

/* Activate/deactivate the current single-core TTBR0 address space. */
int32_t fahim_address_space_activate(uint32_t handle);
int32_t fahim_address_space_deactivate_current(void);
int32_t fahim_address_space_switch(uint32_t next_handle);

/* EL0 SVC return trampoline bookkeeping (used by arch/arm64 entry assembly). */
int32_t fahim_address_space_set_user_return_pc(uint64_t return_pc);
uint64_t fahim_address_space_user_return_pc(void);

/* Checked EL0 memory access through the current process's TTBR1 physical alias. */
int32_t fahim_address_space_validate_user_range_current(uint64_t user_address,
                                                        size_t length,
                                                        uint32_t write);
int32_t fahim_address_space_copy_from_user_current(void *kernel_destination,
                                                    uint64_t user_source,
                                                    size_t length);
int32_t fahim_address_space_copy_to_user_current(uint64_t user_destination,
                                                  const void *kernel_source,
                                                  size_t length);

int32_t fahim_address_space_map_user_page(uint32_t handle,
                                          uint64_t virtual_address,
                                          uint64_t physical_address,
                                          uint32_t permissions);
int32_t fahim_address_space_unmap_user_page(uint32_t handle,
                                            uint64_t virtual_address);
int32_t fahim_address_space_destroy(uint32_t handle);

/* QEMU-only self-test for independent address spaces and mapping lifecycle. */
int32_t fahim_address_space_self_test(void);

#endif
