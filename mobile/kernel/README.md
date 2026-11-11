# Fahim OS Kernel

This is the device-independent ARM64 kernel implementation. The root-level x86 kernel.cpp is a separate learning prototype and is not reused as the mobile kernel.

## Verified milestone 1: ARM64 boot + serial console

- AArch64 assembly entry point and bootstrap stack
- Freestanding C kernel entry
- Flattened Device Tree (FDT) header and structure parsing
- PL011 UART discovery through Device Tree compatible and reg properties
- Polled serial text output

## Verified milestone 2: FDT RAM discovery

- Reads enabled Device Tree nodes with device_type = "memory"
- Uses parent bus address-cells and size-cells to decode each reg tuple
- Supports one-cell and two-cell addresses and sizes
- Handles multiple RAM tuples and rejects malformed or overflowing ranges

## Milestone 3: reserved memory exclusions

The memory-layout builder combines FDT memreserve entries, enabled static children of reserved-memory, the complete kernel image, and the DTB blob. It merges reservations and subtracts them from discovered RAM to produce usable ranges. Dynamic reserved-memory allocations are rejected until a placement allocator exists.

## Verified milestone 4: ARM64 exception vectors and fatal diagnostics

The boot path clears BSS, normalizes EL2 entry to EL1h, installs a 2 KiB-aligned 16-slot VBAR_EL1 vector table, and reports fatal exception state over PL011. CI triggers a controlled BRK #0xF05 and checks the expected vector and syndrome. The current-EL SPx IRQ vector now saves the interrupted general-purpose register frame, calls the IRQ dispatcher, restores the frame, and returns with ERET.

## Milestone 5A: GICv3 platform discovery

The QEMU reference machine is explicitly configured with gic-version=3. The kernel discovers the enabled arm,gic-v3 node from the boot Device Tree, validates its register tuples, and reports the Distributor and Redistributor MMIO regions.

The parser accepts one or more Redistributor regions up to the early-boot limit and validates optional #redistributor-regions and redistributor-stride properties.

## Verified milestone 5B: upstream GICv3 driver integration

Fahim OS integrates the maintained arm-gic 0.10.0 driver from ArmFirmwareCrates / TrustedFirmware.org through a small no_std Rust static-library bridge. The driver remains upstream code; Fahim OS supplies only the C ABI adapter and build integration.

The QEMU single-core path validates the current register-window layout, constructs the upstream GICv3 driver, initializes the Redistributor and Distributor, enables Group 1 for the boot CPU, and configures the CPU priority mask. The EL2 entry path sets ICC_SRE_EL2.SRE and ICC_SRE_EL2.Enable so EL1 can access the GIC CPU-interface system registers.

The bridge currently targets one contiguous Redistributor region and one boot CPU, matching the current QEMU reference setup. Other topologies are rejected rather than silently misconfigured.

## Verified milestone 5C: IRQ dispatcher + ARM Generic Timer

The interrupt path uses the existing upstream arm-gic CPU-interface API. GicCpuInterface::handle_interrupt(InterruptGroup::Group1, ...) performs interrupt acknowledge, runs the source handler, and automatically writes EOI after the handler returns. The exception stub preserves x0-x30, restores the interrupted frame, and uses ERET to resume the interrupted kernel context.

The EL1 Physical Timer is implemented by the upstream TrustedFirmware.org arm-generic-timer 0.3.1 crate (sysreg::PhysicalTimer and its Timer abstraction). The kernel configures timer PPI INTID 30 as level-triggered through arm-gic, then runs a five-interrupt QEMU test at a 10 ms interval. Each timer event reprograms the timer before the upstream GIC handler issues EOI; after the fifth event, the timer interrupt is masked. CPU IRQ delivery is unmasked only after the GIC, timer and exception dispatcher are ready.

This test proves the single-core QEMU reference path for timer programming, IRQ exception entry/return, GIC acknowledge, source dispatch, source deassertion and EOI. It does not yet prove SMP routing or physical-phone interrupt behavior.

Third-party source and licensing details are recorded in THIRD_PARTY_NOTICES.md. The upstream GIC and Generic Timer crates are dual-licensed under MIT OR Apache-2.0.

## Verified milestone 6A: physical page allocator

The kernel integrates upstream frame-alloc 0.1.0, a dependency-free no_std physical frame allocator. Fahim OS supplies the boot memory map as sorted, non-overlapping, 4 KiB-aligned usable ranges. The upstream SummaryBuddyAllocator keeps its bitmap in usable RAM, preserves every gap/reservation, and supports contiguous naturally-aligned frame allocations without a heap.

The Rust bridge uses an identity physical-memory provenance adapter. Allocator initialization still occurs before MMU enable, when the initial usable RAM ranges are identity-addressable and the allocator bitmap can be safely placed. After MMU enable, the kernel TTBR0 mapping deliberately retains supervisor-only identity aliases for usable RAM, so page-table frames and other kernel-owned frames remain accessible through EL1 while address spaces are prepared. QEMU self-tests allocate one 4 KiB page and one contiguous four-page block, verify alignment and that both remain inside registered usable ranges, write/read physical memory, free both allocations, allocate again, and confirm free capacity is restored.

The upstream crate is version 0.1.0 and explicitly marks its API unstable. Its published documentation reports Miri, concurrency, and audit testing, but its kernel demo is x86-64; ARM integration is not claimed upstream. Fahim OS validates this integration separately on the ARM64 QEMU reference machine. SMP, interrupt-time allocation, and physical-phone memory mapping are not yet claimed.

## Milestone 6B: AArch64 EL1 MMU and page tables

The kernel integrates upstream aarch64-paging 0.12.3 with a custom no-heap Translation adapter backed by the physical page allocator. Page-table frames are allocated as 4 KiB physical pages and remain owned by a static Mapping object for the lifetime of the active TTBR0 translation.

The reference map keeps a 48-bit lower-VA identity mapping for the running boot PC and stack, and adds a 48-bit upper-VA direct-map alias through TTBR1. Both use 4 KiB granules. FDT-discovered RAM is mapped as cacheable Normal-WB memory; FDT-discovered GIC Distributor/Redistributor windows and the PL011 UART are mapped as Device-nGnRnE in both ranges. Linker-provided kernel text and rodata boundaries receive separate RO/executable and RO/XN permissions; data and BSS remain RW/XN.

QEMU verification checks continued execution after SCTLR_EL1.M is enabled, tests physical-page read/write through the lower identity map, and separately tests read/write through a TTBR1 high-half alias. The kernel now executes through the TTBR1 high-half alias while retaining lower identity mappings for kernel data, boot DTB and MMIO references. Isolated EL0 page tables are prepared but are not yet activated for user execution.

The upstream page-table crate is MIT OR Apache-2.0 and is not an officially supported Google product. Third-party details and the exact pinned version are in THIRD_PARTY_NOTICES.md.

## Verified milestone 6C: TTBR1 high-half direct-map alias

The kernel builds a second upstream aarch64-paging Mapping for VaRange::Upper and installs it through TTBR1. It maps discovered RAM at 0xffff_0000_0000_0000 + physical_address, and maps discovered GIC/PL011 windows using Device-nGnRnE attributes. Kernel text and rodata retain the same RO/X and RO/XN permissions across both aliases. TCR_EL1.EPD1 is cleared so TTBR1 table walks are enabled.

A QEMU self-test allocates a physical page, writes and reads a pattern through the high-half alias, then releases the frame. TTBR0's lower identity map remains active for the current boot PC and stack. This is a verified high-half direct-map alias, not yet a relocated high-half kernel or an EL0 process-address-space implementation.


## Milestone 6D: EL0 address-space lifecycle

The kernel now provides a small address-space manager on top of the pinned upstream aarch64-paging 0.12.3 implementation. It creates independent TTBR0 lower-VA page tables with unique, reusable ASIDs and generation-tagged handles. Every new root preserves the current supervisor-only low identity map for RAM and Device MMIO, including the kernel text/rodata permissions, while the existing TTBR1 high-half direct map remains shared.

The first user mapping API supports 4 KiB EL0 pages with either read/write + execute-never data permissions or read-only executable code permissions. User virtual addresses are restricted to a separate 4 GiB–1 TiB range and checked against all kernel RAM/MMIO identity ranges. Unmapping uses the upstream page-table crate's invalid-descriptor mapping path; empty subtables are compacted. Destroying an inactive address space returns its page-table frames and releases its ASID. Mapped data frames remain caller-owned.

QEMU tests create two address spaces, map the same virtual address to different physical pages with different EL0 permissions, reject a duplicate mapping, unmap one space while verifying the other remains intact, remap, destroy both spaces, and verify the user frames can be returned to the physical allocator.

This milestone prepares independent page tables; it does not yet activate a user address space, enter EL0, provide a process stack/context switch, or prove user-mode execution. Those require the kernel to relocate its active PC/stack to the high-half mapping and add the EL0 exception-return/scheduler path. SMP TLB shootdown is not implemented.


## Milestone 6E: high-half kernel execution

After building the existing TTBR1 direct map, the boot path now transfers live EL1 execution to the high-half alias without relocating the physical image. A small AArch64 transition routine preserves the AAPCS64 callee-saved registers, rebases the active stack and caller return address, moves VBAR_EL1 to the high-half exception-vector alias, invalidates the instruction cache, and branches to the same code through TTBR1. The lower identity mappings remain present for existing physical/DTB/MMIO pointers and for prepared process roots.

QEMU checks the executing PC, active SP and VBAR_EL1 all resolve through the upper direct-map window. The production and controlled-exception boots then continue through the relocated kernel. This validates high-half execution and exception-vector relocation on the QEMU single-core reference platform; it does not yet enter EL0 or provide a scheduler/context switch.


## Milestone 6F: first EL0 execution and SVC return

The kernel now activates a prepared TTBR0 address space, loads an EL0 stack pointer and enters a small AArch64 user payload with ERET. The lower-EL AArch64 synchronous vector handles the dedicated SVC exit used by the smoke test. The handler reads the return trampoline address, deactivates the process mapping through the upstream paging crate (restoring the previous kernel TTBR0 and invalidating the process ASID), and changes ELR_EL1/SPSR_EL1 so ERET resumes the saved EL1 caller. The user payload returns a known value in x0 to prove that the EL0 instruction stream actually ran.

The payload is assembled by the existing AArch64 toolchain into kernel rodata, copied to a separately allocated frame, mapped as user read-only executable, and paired with a user read/write execute-never stack page. QEMU verifies successful EL0 entry, SVC exit, kernel TTBR0 restoration, address-space destruction and physical-frame release in both production and controlled-exception boots.

This is a single-core, one-shot EL0 execution test, not a scheduler or general syscall ABI. User faults other than the dedicated exit SVC still take the fatal diagnostic path. Multiple runnable processes, timer preemption in EL0, process context switching and SMP TLB shootdown remain future work.


## Milestone 7A: first EL0 process scheduler and syscall dispatcher

Fahim OS now includes an AArch64 callee-saved context-switch primitive adapted from the small scheduler implementation in KNCn23/mini-rtos. Each task has a private EL1 kernel stack and a saved AArch64 context (x19-x30 and SP). The scheduler uses a round-robin ready-task picker and switches the current TTBR0 through the existing ASID-aware upstream paging bridge; TTBR1 remains the shared high-half kernel mapping. The lower-EL exception frame now saves ELR_EL1, SPSR_EL1 and SP_EL0 on each task's kernel stack so a suspended SVC can resume with the correct user return state.

A table-driven AArch64 SVC dispatcher uses x8 for the syscall number, x0-x1 for arguments and x0 for the result. The initial minimal ABI exposes EXIT, YIELD, GETPID and LOG; unknown numbers return -ENOSYS. It is an Fahim OS-specific bootstrap ABI, not yet a POSIX or Linux syscall table.

QEMU boots two EL0 tasks with separate TTBR0 address spaces and stacks while sharing one read-only executable page. Their syscall sequence verifies task IDs, round-robin yield, switching TTBR0 between distinct ASIDs, task exit, return to the boot kernel context and cleanup. This milestone verifies cooperative task switching. Timer-driven preemption, blocking waits/sleep, process reaping policies, user-fault isolation and SMP scheduling are not yet implemented.

## Reference platform and verification

GitHub Actions cross-compiles and boots the kernel in QEMU virt with 512 MiB RAM and GICv3 explicitly selected. It checks the RAM/reserved/usable ranges, GICv3 initialization, five timer IRQ deliveries, and the controlled synchronous-exception test.

[Open the ARM64 QEMU workflow](https://github.com/mrfahimhawladar/OS/actions/workflows/arm64-kernel.yml).

## Build locally

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu-

Build the separate exception-test image with:

    make -C mobile/kernel CROSS_COMPILE=aarch64-linux-gnu- EXCEPTION_SELFTEST=1

Production image: mobile/kernel/build/arm64-qemu/fahim-kernel.elf

Exception-test image: mobile/kernel/build/arm64-qemu-exception-test/fahim-kernel.elf

## Important limits

- Physical pages are now allocated from validated, page-aligned usable RAM on the single-core QEMU target. The bridge is identity-map-only and intentionally rejects initialization once the EL1 MMU is enabled.
- Dynamic reserved-memory allocations and page-aligned allocation are not supported.
- The current exception diagnostics are fatal for synchronous exceptions, FIQ and SError. Only the current-EL SPx IRQ vector has a returning dispatcher path so far.
- The kernel now runs through its TTBR1 high-half direct-map alias and can enter one prepared EL0 address space for a one-shot SVC smoke test. The lower supervisor identity mappings remain for current kernel data and platform references. General syscalls, user-fault recovery, scheduler/process context switching, demand paging, guard-page policy and SMP TLB shootdown are not implemented yet.
- The upstream GICv3 driver is currently initialized for the single-core QEMU layout. The active dispatcher handles the EL1 physical timer PPI; general device IRQ registration, SPI routing, SMP and interrupt nesting remain future work.
- The generic timer is now functional on the QEMU reference target. The scheduler, heap, storage, input, graphics, and touchscreen are not implemented.
- This is not an Android image, phone ROM, or flashable artifact. QEMU success does not prove support for a real smartphone.

## Next milestones

1. Add timer-preemptive scheduling, task sleep/wakeup and safe task reaping
2. Add general syscall argument validation, user-fault isolation and richer process lifecycle
3. Add allocator zones and SMP-safe page allocator publication with TLB shootdown
4. Add scheduler and context switching
5. Add storage, input, display and touchscreen driver frameworks
6. Develop a physical-device platform port after the memory and interrupt foundations are stable
