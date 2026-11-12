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

QEMU boots two EL0 tasks with separate TTBR0 address spaces and stacks while sharing one read-only executable page. Their syscall sequence verifies task IDs, round-robin yield, switching TTBR0 between distinct ASIDs, task exit, return to the boot kernel context and cleanup. The first scheduler milestone verifies cooperative switching. Timer-driven preemption is verified in Milestone 7B; per-task sleep, wake deadlines and idle WFI are verified in Milestone 7C. Process reaping policies, user-fault isolation and SMP scheduling remain future work.


## Milestone 7B: timer-preemptive EL0 scheduling

The upstream Generic Timer bridge now supports both a bounded diagnostic run and a continuing periodic tick source. Its five-interrupt self-test remains bounded; the scheduler can raise the IRQ limit for ongoing 10 ms timer ticks and stop/reinitialize the source safely. Timer PPI 30 is re-armed by the upstream driver before GIC EOI. While EL0 tasks are active, the IRQ dispatcher routes each timer tick to the round-robin scheduler without flooding the serial console.

The two-process QEMU payload no longer calls YIELD. Instead, each task increments the same user-mapped shared counter and waits until the peer increments it too. The first task therefore cannot progress until the timer interrupt preempts it and schedules the second task. Both tasks then complete their syscall sequence and exit. The test also confirms that timer ticks occurred and the shared counter reached two, before both ASID-backed address spaces and their frames are released.

This verifies single-core timer-driven preemption from EL0 on the QEMU virt reference target. Blocking sleep and idle wakeup are added in Milestone 7C. waitpid, user-fault recovery, SMP and a production process loader remain future work.


## Milestone 7C: task sleep, timer wakeup and idle WFI

The scheduler now tracks READY, RUNNING, SLEEPING and DEAD states. The SLEEP syscall accepts a millisecond duration, rounds it up to the 10 ms scheduler tick and parks the current task until its deadline expires. Each timer tick advances the scheduler clock, marks expired tasks READY and uses the same TTBR0/context-switch path to resume them.

When no task is runnable but at least one remains asleep, the scheduler switches to the shared kernel idle continuation, unmasks IRQ delivery and executes WFI. Timer PPI 30 wakes the idle CPU; the IRQ handler can switch directly to an expired task and later resume the interrupted idle continuation. When every task exits, the scheduler returns to the boot caller with IRQ delivery masked.

The QEMU payload makes the two EL0 tasks wait for each other through a deliberately shared data page, then calls SLEEP with different deadlines (30 ms and 50 ms). Both tasks log before sleeping and after waking, and both exit. CI checks both sleep-entry and post-wakeup markers, at least one idle wait, exactly two wakeups, the shared counter and timer tick output in both production and exception-test boots.

This validates a small single-core sleep/deadline scheduler and idle path. It does not yet provide wait queues, waitpid/zombie handling, task slot reuse, process reaping policies, cancellation/signals, user-fault isolation or SMP scheduling.


## Milestone 7D: lower-EL process fault isolation

The lower-EL AArch64 synchronous exception path now distinguishes the normal SVC ABI from unsupported user exceptions. A scheduled task that takes a data abort, instruction abort, undefined instruction, unsupported SVC immediate or another lower-EL synchronous exception is reported with PID, ESR, ELR and FAR, then only that task is marked dead and removed from scheduling. Other runnable or sleeping tasks continue. The dedicated one-shot SVC exit remains available only outside the multi-process scheduler; a scheduled process using that immediate exits through its own task context instead of returning into the boot trampoline.

The QEMU scheduler test adds a third EL0 task. It logs a marker and deliberately attempts to write into its own user read-only executable page. The test requires the kernel to report PID 303's permission fault, kill that task, and still complete the two other processes' shared-memory rendezvous, independent sleep deadlines, wakeups, idle WFI and normal exits. CI checks exactly one user fault and the continued completion of both unaffected tasks in both QEMU boots.

This is basic process-level termination for lower-EL synchronous faults, not a full POSIX signal model. Page-fault recovery, demand paging, copy-in/copy-out, syscall pointer validation, fault retry, user exception delivery and resource reaping remain future work.


## Milestone 7E: parent/child lifecycle, WAITPID and zombie reaping

The process table now records each task's parent PID and signed exit status. A child with a live parent becomes ZOMBIE on exit and retains its address-space handle until the parent collects it. SYS_WAITPID (x0 = child PID, or 0 for any child) blocks the caller when the selected child is still running, sleeping or otherwise not yet exited. Once the child exits, the parent is woken, receives the child PID in x0 and its exit status in x1, and the kernel destroys the child's inactive page-table tree and releases its ASID. An absent matching child returns -ECHILD.

A parent that exits or faults before collecting children reparents them to the kernel. Orphaned zombie address spaces are reclaimed immediately; still-running orphan children are automatically reclaimed when they later exit. Root tasks also have their own inactive TTBR0 page tables destroyed on exit. Mapped data/code/stack frames remain owned by the process creator and are released by that owner after the scheduler test, rather than being freed by the page-table destructor.

QEMU now creates four tasks: parent PID 101, child PID 202, independent sleeper PID 404 and faulting PID 303. The parent blocks in WAITPID; child 202 sleeps 30 ms and exits with status 42; the parent checks both returned PID and status before exiting. PID 404 independently sleeps 50 ms. PID 303 faults on a read-only code page and is terminated without stopping the other tasks. CI checks the parent's post-WAITPID marker, two idle waits/wakeups, one isolated fault, one successful WAITPID and four address-space reaps in both boot variants.

The WAITPID ABI uses x0 for the child PID (or zero for any child) and x1 for an optional user status pointer. On success the child PID is returned in x0 and a POSIX-compatible wait status is copied to the validated writable user buffer. Task slots are fixed-size and are reset for a new scheduler run; runtime task creation/slot reuse, signals, process groups and full user-memory pointer validation remain future work.


## Milestone 7F: checked user-copy and syscall memory boundaries

The address-space bridge now validates each EL0 page covered by a user buffer before copying. Validation checks overflow, the permitted user VA window, a live level-3 mapping, the EL0 USER permission and, for copy-out, that the page is writable. Copies are performed through the shared TTBR1 direct-map alias rather than dereferencing the caller's TTBR0 virtual pointer in EL1. Transfers are capped at 64 KiB per bridge operation.

The initial SYS_WRITE service reads at most 256 bytes from a checked user buffer into a kernel stack buffer before printing to the QEMU console. Invalid/unmapped/kernel-half pointers return EFAULT and are not dereferenced. WAITPID now accepts an optional writable user status pointer; it validates the pointer before blocking, encodes a normal exit or signalled status in the POSIX wait-status format, and copies that value back through the checked copy-to-user path.

QEMU verifies a successful read from the user code page containing "Fahim", rejection of a kernel-half pointer, rejection of a read-only code page as a WAITPID output pointer, then successful WAITPID status copy-out to a writable shared data page. The parent verifies the copied status value 0x2A00 (normal exit code 42) before logging its post-WAITPID marker. CI checks these paths in production and exception-test boot variants.

This is a checked memory-copy foundation, not a full POSIX file-descriptor implementation. SYS_WRITE currently targets the serial console and has a 256-byte service limit; C-string helpers, arbitrary-length I/O, partial-copy recovery, exception-table fixups, demand paging and a complete VFS-backed read/write interface remain future work.

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
- The current exception diagnostics are fatal for kernel synchronous exceptions, FIQ and SError. Lower-EL AArch64 SVC and IRQ vectors preserve per-task trap return state; unsupported lower-EL synchronous exceptions terminate only the current scheduled process. User-fault recovery and cleanup of its owned resources are still incomplete.
- The kernel runs through its TTBR1 high-half direct map and can schedule multiple EL0 tasks with separate ASIDs, private EL1 stacks, SVC services, timer preemption, timed sleep/wakeup, a WFI idle continuation and basic per-process lower-EL fault termination. The lower supervisor identity mappings remain for kernel data and platform references. General user-pointer validation, fault recovery/demand paging, guard pages, process reaping and SMP TLB shootdown are not implemented yet.
- The upstream GICv3 driver is currently initialized for the single-core QEMU layout. The active dispatcher handles the EL1 physical timer PPI; general device IRQ registration, SPI routing, SMP and interrupt nesting remain future work.
- The generic timer and single-core preemptive EL0 scheduler, including timed sleep/wakeup, are functional on the QEMU reference target. A full heap/process ABI, storage, input, graphics, touchscreen and phone-specific device drivers remain to be developed.
- This is not an Android image, phone ROM, or flashable artifact. QEMU success does not prove support for a real smartphone.

## Next milestones

1. Add runtime process creation, task-slot reuse, signals and process groups
2. Add demand paging, fault retry and complete VFS-backed read/write semantics
3. Add ELF64 loader/exec, per-process user stack/TLS and a stable syscall ABI
4. Add kernel heap, memory zones, allocator reclamation and SMP-safe page allocation/TLB shootdown
5. Add storage, VFS, block-device and filesystem layers
6. Add input, display, GPU, audio, touchscreen and power-management driver frameworks
7. Add networking stack and device services
8. Add security boundaries, capability/permission model, executable signing/verification and update/rollback
9. Port the platform layer to a real ARM64 smartphone SoC: boot chain, device tree, clocks, regulators, pinctrl, storage, display, touch, modem, Wi-Fi, audio, thermal and suspend/resume
10. Build the Android-compatible userspace/runtime, graphics composition, system services, app package/install model and developer tooling

## Lock Screen integration boundary

The kernel source is again present in the current main branch and the ARM64 QEMU workflow verifies its existing boot path. The native Lock Screen runtime currently lives in `mobile/systemui/runtime/`, outside the freestanding kernel. Do not link its standard C++ runtime directly into the kernel image.

A future trusted system-process launch path must:
1. Obtain a platform-approved display mapping from a framebuffer/virtio-GPU or phone display driver.
2. Obtain normalized input events from virtio-input or the board touchscreen driver.
3. Create the compositor and Lock Screen session before exposing ordinary application sessions.
4. Map application surfaces/input only through a kernel-enforced locked-session policy.
5. Delegate PIN verification and persistent lockout policy to protected credential storage/TEE; there is intentionally no default PIN or software-only production verifier.

Until the process loader, graphics/input drivers and trusted credential backend exist, the runtime tests prove component behavior only—not a boot-time security boundary.

## Kernel-enforced session lock gate

The scheduler now starts with the session locked. Each user task has a kernel-assigned role: APPLICATION, LOCKSCREEN, or EMERGENCY. While locked, `pick_next_task()` will not select APPLICATION tasks; only LOCKSCREEN and EMERGENCY roles remain runnable. The default `fahim_scheduler_create_user()` assigns APPLICATION. A trusted kernel process manager must explicitly use `fahim_scheduler_create_user_with_role()` when creating the lock-screen service.

The EL0 SVC ABI now exposes:
- `x8 = 8`: GET_SESSION_LOCKED; returns 1 when locked, 0 when unlocked.
- `x8 = 9, x0 = 0|1`: SET_SESSION_LOCKED. Only the currently running kernel-assigned LOCKSCREEN role may change the session gate; application and emergency roles receive an error.

The QEMU kernel self-test verifies the default locked boot policy, app-role denial, trusted-role allowance and explicit lock/unlock transitions. The process scheduler smoke test explicitly unlocks only for its synthetic test tasks and restores the locked state before returning.

This is a kernel scheduler gate foundation. A trusted task loader/process manager and boot-created Lock Screen task are still required before it can protect a real interactive session.
