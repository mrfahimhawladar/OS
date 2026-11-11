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
use core::{
    panic::PanicInfo,
    ptr::NonNull,
    sync::atomic::{AtomicBool, AtomicU32, Ordering},
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

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("wfe", options(nomem, nostack)) };
    }
}
