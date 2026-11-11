#![no_std]

use arm_gic::{
    gicv3::{
        registers::{Gicd, GicrSgi},
        GicCpuInterface, GicV3,
    },
    UniqueMmioPointer,
};
use core::{
    panic::PanicInfo,
    ptr::NonNull,
    sync::atomic::{AtomicBool, Ordering},
};

static GIC_INITIALIZED: AtomicBool = AtomicBool::new(false);

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

    // SAFETY: The C caller guarantees that the Device Tree supplied these
    // physical MMIO addresses, that they are mapped as device memory, and
    // that no other live owner accesses the same GIC register windows.
    let gicd: UniqueMmioPointer<'static, Gicd> =
        unsafe { UniqueMmioPointer::new(gicd_pointer) };

    // SAFETY: The same platform/MMIO validity contract applies to GICD and
    // GICR. The early QEMU platform supplies one contiguous GICR region.
    let mut gic: GicV3<'static> =
        match unsafe { GicV3::new(gicd, gicr_pointer, cpu_count) } {
            Ok(driver) => driver,
            Err(_) => return -3,
        };

    // This performs the upstream Redistributor, Distributor and Group 1
    // CPU-interface setup; it does not unmask CPU IRQ exceptions in DAIF.
    gic.setup(0);
    GicCpuInterface::set_priority_mask(0xff);

    GIC_INITIALIZED.store(true, Ordering::Release);
    0
}

/// Return the current CPU interface priority mask, or u32::MAX before init.
#[no_mangle]
pub extern "C" fn fahim_gicv3_read_priority_mask() -> u32 {
    if !GIC_INITIALIZED.load(Ordering::Acquire) {
        return u32::MAX;
    }

    GicCpuInterface::get_priority_mask() as u32
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("wfe", options(nomem, nostack)) };
    }
}
