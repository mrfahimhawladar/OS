#![no_std]

use arm_gic::{
    gicv3::{
        registers::{Gicd, GicrSgi},
        GicCpuInterface, GicV3,
    },
    IntId, InterruptGroup, Trigger, UniqueMmioPointer,
};
use arm_generic_timer::{
    sysreg::PhysicalTimer,
    Timer, TimerInterface,
};
use frame_alloc::{AllocatorStats, PageSize, PhysRange, PhysicalAllocator, Provenance, RegionInit, SummaryBuddyAllocator};
use core::{
    panic::PanicInfo,
    ptr::NonNull,
    sync::atomic::{AtomicBool, AtomicU32, AtomicUsize, Ordering},
    time::Duration,
};

const EL1_PHYSICAL_TIMER_INTID: u32 = 30;
const TIMER_PPI_INDEX: u32 = EL1_PHYSICAL_TIMER_INTID - 16;
const TIMER_TEST_IRQ_LIMIT: u32 = 5;
const NO_PENDING_INTERRUPT: u32 = u32::MAX;

static GIC_INITIALIZED: AtomicBool = AtomicBool::new(false);
static TIMER_INITIALIZED: AtomicBool = AtomicBool::new(false);
static TIMER_INTERVAL_MS: AtomicU32 = AtomicU32::new(0);
static TIMER_FREQUENCY_HZ: AtomicU32 = AtomicU32::new(0);
static TIMER_IRQ_COUNT: AtomicU32 = AtomicU32::new(0);

/// Initialize the upstream GICv3 driver for the boot CPU.
///
/// # Safety
/// The caller must pass valid, non-overlapping, device-memory GICD and GICR
/// register windows from the active platform Device Tree. The windows must
/// remain mapped for the lifetime of the kernel. The supplied CPU count must
/// match the contiguous Redistributor frames beginning at gicr_base.
#[no_mangle]
pub unsafe extern "C" fn fahim_gicv3_init(
    gicd_base: u64,
    gicr_base: u64,
    cpu_count: usize,
) -> i32 {
    if GIC_INITIALIZED.load(Ordering::Acquire) {
        return -1;
    }
    if gicd_base == 0 || gicr_base == 0 || cpu_count == 0 {
        return -2;
    }

    let Some(gicd_pointer) = NonNull::new(gicd_base as *mut Gicd) else {
        return -2;
    };
    let Some(gicr_pointer) = NonNull::new(gicr_base as *mut GicrSgi) else {
        return -2;
    };

    /* SAFETY: the C caller validates the Device Tree MMIO windows. */
    let gicd: UniqueMmioPointer<'static, Gicd> =
        unsafe { UniqueMmioPointer::new(gicd_pointer) };

    /* SAFETY: early QEMU provides one contiguous GICR region. */
    let mut gic: GicV3<'static> =
        match unsafe { GicV3::new(gicd, gicr_pointer, cpu_count) } {
            Ok(driver) => driver,
            Err(_) => return -3,
        };

    gic.setup(0);
    GicCpuInterface::set_priority_mask(0xff);

    /* EL1 physical timer is PPI INTID 30, i.e. PPI index 14. */
    let timer_intid = IntId::ppi(TIMER_PPI_INDEX);
    if gic.set_interrupt_priority(timer_intid, Some(0), 0x80).is_err() {
        return -4;
    }
    if gic.set_trigger(timer_intid, Some(0), Trigger::Level).is_err() {
        return -5;
    }
    if gic.enable_interrupt(timer_intid, Some(0), true).is_err() {
        return -6;
    }

    GIC_INITIALIZED.store(true, Ordering::Release);
    0
}

#[no_mangle]
pub extern "C" fn fahim_gicv3_read_priority_mask() -> u32 {
    if !GIC_INITIALIZED.load(Ordering::Acquire) {
        return u32::MAX;
    }
    GicCpuInterface::get_priority_mask() as u32
}

/// Set up the upstream ARM Generic Timer EL1 Physical Timer.
///
/// The timer is armed with DAIF.I still set. CPU IRQ delivery is enabled by C
/// only after the GIC and exception-return dispatcher are ready.
#[no_mangle]
pub extern "C" fn fahim_generic_timer_init(interval_ms: u32) -> i32 {
    if !GIC_INITIALIZED.load(Ordering::Acquire) {
        return -1;
    }
    if interval_ms == 0 || interval_ms > 1000 {
        return -2;
    }
    if TIMER_INITIALIZED.load(Ordering::Acquire) {
        return -3;
    }

    /* SAFETY: early boot is single-core and IRQ delivery is still masked. */
    let physical_timer = unsafe { PhysicalTimer::new() };
    let frequency = physical_timer.frequency();
    if frequency == 0 {
        return -4;
    }

    let mut timer = Timer::new(physical_timer);
    timer.set_remaining_time(Duration::from_millis(u64::from(interval_ms)));
    timer.enable_interrupt();
    timer.enable();

    TIMER_INTERVAL_MS.store(interval_ms, Ordering::Relaxed);
    TIMER_FREQUENCY_HZ.store(frequency, Ordering::Relaxed);
    TIMER_IRQ_COUNT.store(0, Ordering::Relaxed);
    TIMER_INITIALIZED.store(true, Ordering::Release);
    0
}

#[no_mangle]
pub extern "C" fn fahim_generic_timer_frequency_hz() -> u32 {
    TIMER_FREQUENCY_HZ.load(Ordering::Acquire)
}

#[no_mangle]
pub extern "C" fn fahim_generic_timer_irq_count() -> u32 {
    TIMER_IRQ_COUNT.load(Ordering::Acquire)
}

#[no_mangle]
pub extern "C" fn fahim_generic_timer_stop() {
    if !TIMER_INITIALIZED.load(Ordering::Acquire) {
        return;
    }

    /* SAFETY: caller masks IRQ delivery before stopping this single-core timer. */
    let physical_timer = unsafe { PhysicalTimer::new() };
    let mut timer = Timer::new(physical_timer);
    timer.disable_interrupt();
}

/// Program the next relative deadline before GIC EOIR drops active priority.
fn rearm_physical_timer() {
    let interval_ms = TIMER_INTERVAL_MS.load(Ordering::Relaxed);

    /* SAFETY: IRQ entry masks DAIF; this is the only CPU using this timer. */
    let physical_timer = unsafe { PhysicalTimer::new() };
    let mut timer = Timer::new(physical_timer);
    timer.set_remaining_time(Duration::from_millis(u64::from(interval_ms)));
}

/// Acknowledge, dispatch and EOI one Group 1 IRQ using the upstream arm-gic API.
#[no_mangle]
pub extern "C" fn fahim_gicv3_dispatch_irq() -> u32 {
    if !GIC_INITIALIZED.load(Ordering::Acquire) {
        return NO_PENDING_INTERRUPT;
    }

    let mut handled_intid = NO_PENDING_INTERRUPT;
    let handled = GicCpuInterface::handle_interrupt(InterruptGroup::Group1, |intid| {
        handled_intid = intid.raw_value();

        if intid == IntId::ppi(TIMER_PPI_INDEX)
            && TIMER_INITIALIZED.load(Ordering::Acquire)
        {
            let tick = TIMER_IRQ_COUNT.fetch_add(1, Ordering::AcqRel) + 1;
            if tick < TIMER_TEST_IRQ_LIMIT {
                rearm_physical_timer();
            } else {
                /* Stop the level signal before the driver's automatic EOI. */
                let physical_timer = unsafe { PhysicalTimer::new() };
                let mut timer = Timer::new(physical_timer);
                timer.disable_interrupt();
            }
        }
    });

    if handled {
        handled_intid
    } else {
        NO_PENDING_INTERRUPT
    }
}


/* Physical page allocator: 4 KiB frames, using the upstream frame-alloc crate. */
const PAGE_SIZE: PageSize = PageSize::from_log2(12);
const PAGE_BYTES: usize = PAGE_SIZE.bytes();
const PAGE_MASK: usize = PAGE_BYTES - 1;
const PAGE_ORDERS: usize = 15; // 4 KiB through 64 MiB allocations
const PAGE_RANGE_CAPACITY: usize = 40;

#[repr(C)]
#[derive(Clone, Copy)]
struct CMemoryRegion {
    base: u64,
    size: u64,
}

struct IdentityPhysicalMemory;

/*
 * The initial QEMU boot path runs with SCTLR_EL1.M clear, so physical RAM is
 * identity-addressable. Use the same opaque provenance technique documented
 * by the upstream crate's kernel demo; a plain integer-to-pointer cast is not
 * sufficient for Rust's pointer model.
 */
unsafe impl Provenance for IdentityPhysicalMemory {
    unsafe fn create(phys: usize) -> NonNull<u8> {
        let mut pointer = core::ptr::without_provenance_mut::<u8>(phys);
        // SAFETY: opaque asm leaves the address unchanged and establishes a
        // fresh allocation provenance for the identity-mapped physical range.
        unsafe {
            core::arch::asm!("/* {0} */", inout(reg) pointer, options(nostack, preserves_flags));
            NonNull::new_unchecked(pointer)
        }
    }

    unsafe fn destroy<T>(pointer: NonNull<T>) -> usize {
        let address = pointer.addr().get();
        let raw = pointer.as_ptr();
        // SAFETY: consume the pointer through opaque asm so the allocation
        // provenance does not remain live after ownership is returned.
        unsafe {
            core::arch::asm!("/* {0} */", in(reg) raw, options(nostack, preserves_flags));
        }
        address
    }
}

static PAGE_ALLOCATOR: SummaryBuddyAllocator<PAGE_ORDERS, IdentityPhysicalMemory> =
    SummaryBuddyAllocator::new(PAGE_SIZE);
static PAGE_ALLOCATOR_READY: AtomicBool = AtomicBool::new(false);
static PAGE_RANGE_COUNT: AtomicUsize = AtomicUsize::new(0);
static PAGE_RANGE_BASES: [AtomicUsize; PAGE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; PAGE_RANGE_CAPACITY];
static PAGE_RANGE_LENGTHS: [AtomicUsize; PAGE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; PAGE_RANGE_CAPACITY];

/// Initialise from the validated usable-memory ranges emitted by fdt_memory.c.
/// The current early-boot implementation requires the EL1 MMU to remain off.
#[no_mangle]
pub extern "C" fn fahim_page_allocator_init(
    regions: *const CMemoryRegion,
    region_count: usize,
) -> i32 {
    if PAGE_ALLOCATOR_READY.load(Ordering::Acquire) {
        return -1;
    }
    if regions.is_null() || region_count == 0 || region_count > PAGE_RANGE_CAPACITY {
        return -2;
    }

    let sctlr: u64;
    // SAFETY: SCTLR_EL1 is readable from the current EL1 exception level.
    unsafe {
        core::arch::asm!(
            "mrs {value}, SCTLR_EL1",
            value = out(reg) sctlr,
            options(nomem, nostack, preserves_flags)
        );
    }
    if (sctlr & 1) != 0 {
        return -3;
    }

    // SAFETY: the C caller passes the actual bounded memory_layout.usable array.
    let input = unsafe { core::slice::from_raw_parts(regions, region_count) };
    let mut usable = [PhysRange { base: 0, len: 0 }; PAGE_RANGE_CAPACITY];
    let mut count = 0usize;
    let mut previous_end = 0usize;

    for region in input {
        let Ok(base) = usize::try_from(region.base) else { return -4; };
        let Ok(size) = usize::try_from(region.size) else { return -4; };
        let Some(raw_end) = base.checked_add(size) else { return -4; };
        let Some(rounded_base) = base.checked_add(PAGE_MASK) else { return -4; };
        let start = rounded_base & !PAGE_MASK;
        let end = raw_end & !PAGE_MASK;

        if end <= start {
            continue;
        }
        if count >= PAGE_RANGE_CAPACITY || (count != 0 && start < previous_end) {
            return -5;
        }
        usable[count] = PhysRange { base: start, len: end - start };
        previous_end = end;
        count += 1;
    }

    if count == 0 {
        return -6;
    }
    let span_base = usable[0].base;
    let Some(span_end) = usable[count - 1].base.checked_add(usable[count - 1].len) else {
        return -7;
    };
    let span_len = span_end - span_base;

    // SAFETY: this runs once, single-threaded, with IRQ masked. The FDT parser
    // produced sorted, disjoint RAM-minus-reservations ranges. The MMU is off,
    // making physical addresses identity-accessible. The upstream allocator
    // keeps gaps unavailable and carves its bitmap from one usable range.
    if unsafe { PAGE_ALLOCATOR.try_init(span_base, span_len, &usable[..count]) }.is_err() {
        return -8;
    }

    for index in 0..count {
        PAGE_RANGE_BASES[index].store(usable[index].base, Ordering::Relaxed);
        PAGE_RANGE_LENGTHS[index].store(usable[index].len, Ordering::Relaxed);
    }
    PAGE_RANGE_COUNT.store(count, Ordering::Relaxed);
    PAGE_ALLOCATOR_READY.store(true, Ordering::Release);
    0
}

/// Allocate count contiguous 4 KiB physical pages. u64::MAX indicates failure.
#[no_mangle]
pub extern "C" fn fahim_page_allocator_alloc(count: usize) -> u64 {
    if !PAGE_ALLOCATOR_READY.load(Ordering::Acquire) {
        return u64::MAX;
    }
    let Some(count) = core::num::NonZeroUsize::new(count) else {
        return u64::MAX;
    };
    match PAGE_ALLOCATOR.allocate_physical(PAGE_SIZE, count) {
        Ok(address) => address as u64,
        Err(_) => u64::MAX,
    }
}

/// Free a live allocation returned by fahim_page_allocator_alloc.
///
/// # Safety
/// Address and count must exactly match a live allocation and it must no longer
/// be accessed after this call.
#[no_mangle]
pub unsafe extern "C" fn fahim_page_allocator_free(address: u64, count: usize) -> i32 {
    if !PAGE_ALLOCATOR_READY.load(Ordering::Acquire) || address == u64::MAX {
        return -1;
    }
    let Some(count) = core::num::NonZeroUsize::new(count) else { return -2; };
    let Ok(address) = usize::try_from(address) else { return -3; };
    // SAFETY: forwarded from this function's C ABI safety contract.
    unsafe { PAGE_ALLOCATOR.deallocate_physical(PAGE_SIZE, count, address) };
    0
}

#[no_mangle]
pub extern "C" fn fahim_page_allocator_total_bytes() -> u64 {
    if PAGE_ALLOCATOR_READY.load(Ordering::Acquire) {
        PAGE_ALLOCATOR.total_bytes() as u64
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn fahim_page_allocator_free_bytes() -> u64 {
    if PAGE_ALLOCATOR_READY.load(Ordering::Acquire) {
        PAGE_ALLOCATOR.free_bytes() as u64
    } else {
        0
    }
}

fn page_is_usable(address: usize, bytes: usize) -> bool {
    let Some(end) = address.checked_add(bytes) else { return false; };
    let count = PAGE_RANGE_COUNT.load(Ordering::Acquire);
    for index in 0..count {
        let base = PAGE_RANGE_BASES[index].load(Ordering::Relaxed);
        let len = PAGE_RANGE_LENGTHS[index].load(Ordering::Relaxed);
        let Some(range_end) = base.checked_add(len) else { continue; };
        if address >= base && end <= range_end {
            return true;
        }
    }
    false
}

fn verify_page_read_write(address: usize) -> bool {
    // SAFETY: address is a live frame exclusively owned by the self-test.
    let pointer = unsafe { IdentityPhysicalMemory::create(address) }.cast::<u64>();
    let word = pointer.as_ptr();
    // SAFETY: the allocator transferred exclusive ownership of this page.
    unsafe {
        word.write_volatile(0xFA41_1E05_CAFE_BEEF);
        let valid = word.read_volatile() == 0xFA41_1E05_CAFE_BEEF;
        let _ = IdentityPhysicalMemory::destroy(pointer);
        valid
    }
}

/// Verify aligned allocation, contiguous runs, reserved-hole isolation,
/// physical read/write, deallocation and subsequent reuse.
#[no_mangle]
pub extern "C" fn fahim_page_allocator_self_test() -> i32 {
    if !PAGE_ALLOCATOR_READY.load(Ordering::Acquire) {
        return -1;
    }
    let free_before = PAGE_ALLOCATOR.free_bytes();
    let one = match PAGE_ALLOCATOR.allocate_physical(
        PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap()
    ) {
        Ok(value) => value,
        Err(_) => return -2,
    };
    let four = match PAGE_ALLOCATOR.allocate_physical(
        PAGE_SIZE, core::num::NonZeroUsize::new(4).unwrap()
    ) {
        Ok(value) => value,
        Err(_) => {
            // SAFETY: the one-page allocation is still live and exclusively owned.
            unsafe {
                PAGE_ALLOCATOR.deallocate_physical(
                    PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), one
                );
            }
            return -3;
        }
    };

    let one_ok = PAGE_SIZE.is_aligned(one) && page_is_usable(one, PAGE_BYTES);
    let four_ok = four & (PAGE_BYTES * 4 - 1) == 0
        && page_is_usable(four, PAGE_BYTES * 4);
    let non_overlapping = one + PAGE_BYTES <= four || four + PAGE_BYTES * 4 <= one;
    let read_write_ok = verify_page_read_write(one) && verify_page_read_write(four);

    // SAFETY: these addresses and counts are the exact two live allocations above.
    unsafe {
        PAGE_ALLOCATOR.deallocate_physical(
            PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), one
        );
        PAGE_ALLOCATOR.deallocate_physical(
            PAGE_SIZE, core::num::NonZeroUsize::new(4).unwrap(), four
        );
    }

    let reused = match PAGE_ALLOCATOR.allocate_physical(
        PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap()
    ) {
        Ok(value) => value,
        Err(_) => return -4,
    };
    let reuse_ok = PAGE_SIZE.is_aligned(reused) && page_is_usable(reused, PAGE_BYTES);
    // SAFETY: reused is the exact live allocation returned immediately above.
    unsafe {
        PAGE_ALLOCATOR.deallocate_physical(
            PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), reused
        );
    }

    if one_ok && four_ok && non_overlapping && read_write_ok
        && reuse_ok && free_before == PAGE_ALLOCATOR.free_bytes()
    {
        0
    } else {
        -5
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("wfe", options(nomem, nostack)) };
    }
}
