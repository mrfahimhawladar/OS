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
use aarch64_paging::{
    descriptor::{Descriptor, El1Attributes, PhysicalAddress},
    mair::{Mair, MairAttribute, NormalMemory},
    paging::{Constraints, El1And0, MemoryRegion, PageTable, Translation, VaRange},
    Mapping,
};
use core::cell::UnsafeCell;
use core::mem::{align_of, size_of, MaybeUninit};

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
 * Kernel-owned RAM frames remain identity-addressable through the active
 * supervisor-only TTBR0 mappings. Every process address space built here
 * retains that same low identity map, so page-table frames remain accessible
 * while mappings are prepared but inactive. Use the opaque provenance
 * technique documented by the upstream crate's kernel demo; a plain integer-
 * to-pointer cast is not sufficient for Rust's pointer model.
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


const PAGE_TABLE_RECORDS: usize = 64;
const MMU_PAGE_BYTES: usize = 4096;
const HIGH_HALF_DIRECT_MAP_BASE: usize = 0xffff_0000_0000_0000;
const _: [(); MMU_PAGE_BYTES] = [(); size_of::<PageTable<El1Attributes>>()];
const _: [(); MMU_PAGE_BYTES] = [(); align_of::<PageTable<El1Attributes>>()];

#[derive(Clone, Copy, Debug)]
struct PageTableRecord {
    physical: usize,
    pointer: NonNull<PageTable<El1Attributes>>,
}

#[derive(Debug)]
struct KernelTranslation {
    tables: [Option<PageTableRecord>; PAGE_TABLE_RECORDS],
}

impl KernelTranslation {
    const fn new() -> Self {
        Self { tables: [None; PAGE_TABLE_RECORDS] }
    }
}

impl Translation<El1Attributes> for KernelTranslation {
    fn allocate_table(&mut self) -> (NonNull<PageTable<El1Attributes>>, PhysicalAddress) {
        let physical = PAGE_ALLOCATOR
            .allocate_physical(PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap())
            .unwrap_or_else(|_| panic!("out of physical pages for page tables"));
        // SAFETY: the allocator transfers this exclusive 4 KiB frame. The
        // active kernel TTBR0 keeps usable RAM identity-mapped for EL1 access.
        let pointer = unsafe { IdentityPhysicalMemory::create(physical) }
            .cast::<PageTable<El1Attributes>>();
        // SAFETY: a new frame is exclusively owned; an all-zero PageTable is valid.
        unsafe { core::ptr::write_bytes(pointer.as_ptr().cast::<u8>(), 0, MMU_PAGE_BYTES); }

        let Some(slot) = self.tables.iter_mut().find(|slot| slot.is_none()) else {
            // SAFETY: this is the exact live frame allocated above.
            let address = unsafe { IdentityPhysicalMemory::destroy(pointer) };
            unsafe {
                PAGE_ALLOCATOR.deallocate_physical(
                    PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), address
                );
            }
            panic!("page-table record capacity exhausted");
        };
        *slot = Some(PageTableRecord { physical, pointer });
        (pointer, PhysicalAddress(physical))
    }

    unsafe fn deallocate_table(&mut self, pointer: NonNull<PageTable<El1Attributes>>) {
        let Some(slot) = self.tables.iter_mut().find(|slot| {
            slot.as_ref().is_some_and(|record| record.pointer == pointer)
        }) else {
            panic!("attempt to free an unowned page table");
        };
        let record = slot.take().unwrap();
        // SAFETY: pointer is the live allocation returned by allocate_table.
        let physical = unsafe { IdentityPhysicalMemory::destroy(pointer) };
        assert_eq!(physical, record.physical);
        // SAFETY: return the exact 4 KiB frame relinquished above.
        unsafe {
            PAGE_ALLOCATOR.deallocate_physical(
                PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), physical
            );
        }
    }

    fn physical_to_virtual(
        &self,
        physical: PhysicalAddress,
    ) -> NonNull<PageTable<El1Attributes>> {
        self.tables.iter().flatten()
            .find(|record| record.physical == physical.0)
            .map(|record| record.pointer)
            .expect("page-table physical address is not owned")
    }
}

type KernelMapping = Mapping<KernelTranslation, El1And0>;

struct KernelMappingStorage(UnsafeCell<MaybeUninit<KernelMapping>>);
// SAFETY: initialised once with IRQs masked, then read-only for the kernel lifetime.
unsafe impl Sync for KernelMappingStorage {}
static KERNEL_MAPPING: KernelMappingStorage =
    KernelMappingStorage(UnsafeCell::new(MaybeUninit::uninit()));
static KERNEL_UPPER_MAPPING: KernelMappingStorage =
    KernelMappingStorage(UnsafeCell::new(MaybeUninit::uninit()));
static MMU_INITIALIZED: AtomicBool = AtomicBool::new(false);

const NORMAL_RW_XN: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_1)
    .union(El1Attributes::INNER_SHAREABLE)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::PXN)
    .union(El1Attributes::UXN);

const NORMAL_CODE_RO: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_1)
    .union(El1Attributes::INNER_SHAREABLE)
    .union(El1Attributes::READ_ONLY)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::UXN);

const NORMAL_RO_XN: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_1)
    .union(El1Attributes::INNER_SHAREABLE)
    .union(El1Attributes::READ_ONLY)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::PXN)
    .union(El1Attributes::UXN);

const DEVICE_XN: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_0)
    .union(El1Attributes::OUTER_SHAREABLE)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::PXN)
    .union(El1Attributes::UXN);

fn range_is_inside_ram(ram: &[CMemoryRegion], start: usize, end: usize) -> bool {
    if start >= end { return false; }
    ram.iter().any(|region| {
        let Ok(base) = usize::try_from(region.base) else { return false; };
        let Ok(size) = usize::try_from(region.size) else { return false; };
        let Some(region_end) = base.checked_add(size) else { return false; };
        start >= base && end <= region_end
    })
}

fn map_identity_range(
    mapping: &mut KernelMapping,
    base: usize,
    size: usize,
    attributes: El1Attributes,
) -> Result<(), ()> {
    let Some(end) = base.checked_add(size) else { return Err(()); };
    if size == 0 || base & (MMU_PAGE_BYTES - 1) != 0 { return Err(()); }
    mapping.map_range(
        &MemoryRegion::new(base, end),
        PhysicalAddress(base),
        attributes,
        Constraints::empty(),
    ).map_err(|_| ())
}

fn map_high_alias_range(
    mapping: &mut KernelMapping,
    physical_base: usize,
    size: usize,
    attributes: El1Attributes,
) -> Result<(), ()> {
    if size == 0 || physical_base & (MMU_PAGE_BYTES - 1) != 0 {
        return Err(());
    }
    let virtual_base = HIGH_HALF_DIRECT_MAP_BASE
        .checked_add(physical_base)
        .ok_or(())?;
    let virtual_end = virtual_base.checked_add(size).ok_or(())?;
    mapping.map_range(
        &MemoryRegion::new(virtual_base, virtual_end),
        PhysicalAddress(physical_base),
        attributes,
        Constraints::empty(),
    ).map_err(|_| ())
}

/// Build and activate an EL1 identity map from the validated FDT memory map.
/// The mapping is kept alive permanently because its page-table frames are active.
#[no_mangle]
pub extern "C" fn fahim_mmu_init(
    ram_regions: *const CMemoryRegion,
    ram_count: usize,
    device_regions: *const CMemoryRegion,
    device_count: usize,
    text_start: u64,
    text_end: u64,
    rodata_start: u64,
    rodata_end: u64,
) -> i32 {
    if MMU_INITIALIZED.load(Ordering::Acquire) { return -1; }
    if !PAGE_ALLOCATOR_READY.load(Ordering::Acquire)
        || ram_regions.is_null() || device_regions.is_null()
        || ram_count == 0 || ram_count > PAGE_RANGE_CAPACITY
        || device_count == 0 || device_count > 8
    { return -2; }

    let sctlr: u64;
    let mmfr0: u64;
    // SAFETY: both are readable EL1 system registers on AArch64.
    unsafe {
        core::arch::asm!("mrs {v}, SCTLR_EL1", v = out(reg) sctlr, options(nomem, nostack, preserves_flags));
        core::arch::asm!("mrs {v}, ID_AA64MMFR0_EL1", v = out(reg) mmfr0, options(nomem, nostack, preserves_flags));
    }
    if (sctlr & 1) != 0 { return -3; }

    // SAFETY: C passes the actual bounded memory-layout arrays.
    let ram = unsafe { core::slice::from_raw_parts(ram_regions, ram_count) };
    let devices = unsafe { core::slice::from_raw_parts(device_regions, device_count) };
    let (Ok(text_start), Ok(text_end), Ok(rodata_start), Ok(rodata_end)) = (
        usize::try_from(text_start), usize::try_from(text_end),
        usize::try_from(rodata_start), usize::try_from(rodata_end),
    ) else { return -4; };

    if text_start & (MMU_PAGE_BYTES - 1) != 0
        || text_end & (MMU_PAGE_BYTES - 1) != 0
        || rodata_start & (MMU_PAGE_BYTES - 1) != 0
        || rodata_end & (MMU_PAGE_BYTES - 1) != 0
        || !range_is_inside_ram(ram, text_start, text_end)
        || !range_is_inside_ram(ram, rodata_start, rodata_end)
    { return -5; }

    let mut mapping = Mapping::with_asid_and_va_range(
        KernelTranslation::new(), 1, 0, El1And0, VaRange::Lower
    );
    let mut upper_mapping = Mapping::with_asid_and_va_range(
        KernelTranslation::new(), 1, 0, El1And0, VaRange::Upper
    );

    // Keep the lower identity map for the running boot PC and stack, and add
    // a TTBR1 high-half direct-map alias for the same physical RAM.
    for region in ram {
        let (Ok(base), Ok(size)) = (usize::try_from(region.base), usize::try_from(region.size)) else { return -6; };
        if map_identity_range(&mut mapping, base, size, NORMAL_RW_XN).is_err() { return -7; }
        if map_high_alias_range(&mut upper_mapping, base, size, NORMAL_RW_XN).is_err() { return -12; }
    }

    // Device-nGnRnE memory is used for all discovered controller/console registers.
    for region in devices {
        let (Ok(base), Ok(size)) = (usize::try_from(region.base), usize::try_from(region.size)) else { return -8; };
        if map_identity_range(&mut mapping, base, size, DEVICE_XN).is_err() { return -9; }
        if map_high_alias_range(&mut upper_mapping, base, size, DEVICE_XN).is_err() { return -13; }
    }

    // Refine permissions for executable kernel text and read-only constants in both aliases.
    if map_identity_range(&mut mapping, text_start, text_end - text_start, NORMAL_CODE_RO).is_err() {
        return -10;
    }
    if map_high_alias_range(
        &mut upper_mapping, text_start, text_end - text_start, NORMAL_CODE_RO
    ).is_err() {
        return -14;
    }
    if map_identity_range(&mut mapping, rodata_start, rodata_end - rodata_start, NORMAL_RO_XN).is_err() {
        return -11;
    }
    if map_high_alias_range(
        &mut upper_mapping, rodata_start, rodata_end - rodata_start, NORMAL_RO_XN
    ).is_err() {
        return -15;
    }

    const MAIR: Mair = Mair::EMPTY
        .with_attribute(0, MairAttribute::DEVICE_NGNRNE)
        .with_attribute(1, MairAttribute::normal(
            NormalMemory::WriteBackNonTransientReadWriteAllocate,
            NormalMemory::WriteBackNonTransientReadWriteAllocate,
        ));

    // 48-bit lower and upper VA ranges, 4 KiB granules, WB/WA inner-shareable walks.
    // EPD1 stays clear so TTBR1 walks are enabled. Cap IPS at 48 bits because
    // this crate does not use LPA2.
    let ips = (mmfr0 & 0xf).min(5);
    let tcr: u64 = 16
        | (1 << 8) | (1 << 10) | (3 << 12)
        | (16 << 16) | (1 << 24) | (1 << 26) | (3 << 28) | (2 << 30)
        | (ips << 32);

    // SAFETY: both slots are written once with IRQs masked, then remain static.
    unsafe {
        (*KERNEL_MAPPING.0.get()).write(mapping);
        (*KERNEL_UPPER_MAPPING.0.get()).write(upper_mapping);
    }
    // SAFETY: just initialised; neither mapping is moved or dropped.
    let active_mapping = unsafe { (&*KERNEL_MAPPING.0.get()).assume_init_ref() };
    let active_upper_mapping =
        unsafe { (&*KERNEL_UPPER_MAPPING.0.get()).assume_init_ref() };

    let mair_value: u64 = MAIR.into();
    unsafe {
        core::arch::asm!(
            "msr MAIR_EL1, {mair}",
            "msr TCR_EL1, {tcr}",
            "dsb sy",
            "ic iallu",
            "dsb sy",
            "isb",
            mair = in(reg) mair_value,
            tcr = in(reg) tcr,
            options(nostack, preserves_flags)
        );
    }

    // SAFETY: the lower identity map keeps the current PC/stack valid. The
    // upper mapping installs matching RAM and MMIO aliases in TTBR1. Both
    // Mapping objects and their page-table frames remain alive permanently.
    let _previous_ttbr0 = unsafe { active_mapping.activate() };
    let _previous_ttbr1 = unsafe { active_upper_mapping.activate() };

    const SCTLR_RES1: u64 = 0x30d0_0800;
    let sctlr_enabled = sctlr | SCTLR_RES1 | 1 | (1 << 2) | (1 << 12);
    unsafe {
        core::arch::asm!(
            "msr SCTLR_EL1, {value}",
            "isb",
            value = in(reg) sctlr_enabled,
            options(nostack, preserves_flags)
        );
    }

    for index in 0..ram_count {
        KERNEL_RAM_BASES[index].store(
            usize::try_from(ram[index].base).unwrap(), Ordering::Relaxed
        );
        KERNEL_RAM_LENGTHS[index].store(
            usize::try_from(ram[index].size).unwrap(), Ordering::Relaxed
        );
    }
    KERNEL_RAM_REGION_COUNT.store(ram_count, Ordering::Relaxed);
    for index in 0..device_count {
        KERNEL_DEVICE_BASES[index].store(
            usize::try_from(devices[index].base).unwrap(), Ordering::Relaxed
        );
        KERNEL_DEVICE_LENGTHS[index].store(
            usize::try_from(devices[index].size).unwrap(), Ordering::Relaxed
        );
    }
    KERNEL_DEVICE_REGION_COUNT.store(device_count, Ordering::Relaxed);
    KERNEL_TEXT_START.store(text_start, Ordering::Relaxed);
    KERNEL_TEXT_END.store(text_end, Ordering::Relaxed);
    KERNEL_RODATA_START.store(rodata_start, Ordering::Relaxed);
    KERNEL_RODATA_END.store(rodata_end, Ordering::Relaxed);
    MMU_INITIALIZED.store(true, Ordering::Release);
    0
}

#[no_mangle]
pub extern "C" fn fahim_mmu_is_enabled() -> u32 {
    if MMU_INITIALIZED.load(Ordering::Acquire) { 1 } else { 0 }
}

/// Verify one physical page through its TTBR1 high-half direct-map alias.
/// The boot kernel continues executing through TTBR0's identity map.
#[no_mangle]
pub extern "C" fn fahim_mmu_high_alias_self_test() -> i32 {
    if !MMU_INITIALIZED.load(Ordering::Acquire)
        || !PAGE_ALLOCATOR_READY.load(Ordering::Acquire)
    {
        return -1;
    }

    let physical = match PAGE_ALLOCATOR.allocate_physical(
        PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap()
    ) {
        Ok(address) => address,
        Err(_) => return -2,
    };
    let Some(high_virtual) = HIGH_HALF_DIRECT_MAP_BASE.checked_add(physical) else {
        // SAFETY: return the exact page allocated immediately above.
        unsafe {
            PAGE_ALLOCATOR.deallocate_physical(
                PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), physical
            );
        }
        return -3;
    };

    let mut pointer = core::ptr::without_provenance_mut::<u64>(high_virtual);
    // SAFETY: TTBR1 maps this RAM page. The opaque asm establishes compiler
    // provenance for the already mapped high virtual address.
    unsafe {
        core::arch::asm!(
            "/* {0} */",
            inout(reg) pointer,
            options(nostack, preserves_flags)
        );
        core::hint::assert_unchecked(pointer.addr() == high_virtual);

        pointer.write_volatile(0x4849_4748_5641_3031);
        let valid = pointer.read_volatile() == 0x4849_4748_5641_3031;

        // End the temporary high-alias allocation before returning the frame.
        core::arch::asm!(
            "/* {0} */",
            in(reg) pointer,
            options(nostack, preserves_flags)
        );
        PAGE_ALLOCATOR.deallocate_physical(
            PAGE_SIZE, core::num::NonZeroUsize::new(1).unwrap(), physical
        );

        if valid { 0 } else { -4 }
    }
}


/*
 * User address-space lifecycle.
 *
 * Each TTBR0 mapping gets a distinct ASID and an isolated EL0 virtual range.
 * The low identity aliases for RAM and device MMIO are retained with
 * supervisor-only permissions so the current low-linked kernel, its stack,
 * page allocator and exception paths remain usable. TTBR1 continues to hold
 * the shared high-half kernel direct map. These mappings are prepared but are
 * not activated until the kernel has a dedicated EL0 entry/context-switch path.
 */
const ADDRESS_SPACE_CAPACITY: usize = 4;
const USER_VA_START: usize = 0x0000_0001_0000_0000; // 4 GiB
const USER_VA_LIMIT: usize = 0x0000_0100_0000_0000; // 1 TiB, exclusive
const ADDRESS_SPACE_FREE: u32 = 0;
const ADDRESS_SPACE_BUILDING: u32 = 1;
const ADDRESS_SPACE_READY: u32 = 2;
const ADDRESS_SPACE_BUSY: u32 = 3;
const ADDRESS_SPACE_DESTROYING: u32 = 4;
const ADDRESS_SPACE_HANDLE_INDEX_MASK: u32 = 0xff;
const ADDRESS_SPACE_HANDLE_GENERATION_MASK: u32 = 0x00ff_ffff;
const ASID_COUNT: usize = 256;
const ASID_BITMAP_WORDS: usize = ASID_COUNT / 32;
const DEVICE_RANGE_CAPACITY: usize = 8;

struct AddressSpaceSlot {
    state: AtomicU32,
    generation: AtomicU32,
    asid: AtomicUsize,
    mapping: UnsafeCell<MaybeUninit<KernelMapping>>,
}

impl AddressSpaceSlot {
    const fn new() -> Self {
        Self {
            state: AtomicU32::new(ADDRESS_SPACE_FREE),
            generation: AtomicU32::new(0),
            asid: AtomicUsize::new(0),
            mapping: UnsafeCell::new(MaybeUninit::uninit()),
        }
    }
}

// SAFETY: slot state is acquired with atomic compare_exchange before its
// UnsafeCell is accessed. This early kernel is single-core and has no scheduler.
unsafe impl Sync for AddressSpaceSlot {}

static ADDRESS_SPACES: [AddressSpaceSlot; ADDRESS_SPACE_CAPACITY] =
    [const { AddressSpaceSlot::new() }; ADDRESS_SPACE_CAPACITY];

// ASID 0 is reserved; ASID 1 belongs to the boot/kernel mapping.
static ASID_BITMAP: [AtomicU32; ASID_BITMAP_WORDS] = [
    AtomicU32::new(0b11),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

static KERNEL_RAM_REGION_COUNT: AtomicUsize = AtomicUsize::new(0);
static KERNEL_RAM_BASES: [AtomicUsize; PAGE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; PAGE_RANGE_CAPACITY];
static KERNEL_RAM_LENGTHS: [AtomicUsize; PAGE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; PAGE_RANGE_CAPACITY];
static KERNEL_DEVICE_REGION_COUNT: AtomicUsize = AtomicUsize::new(0);
static KERNEL_DEVICE_BASES: [AtomicUsize; DEVICE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; DEVICE_RANGE_CAPACITY];
static KERNEL_DEVICE_LENGTHS: [AtomicUsize; DEVICE_RANGE_CAPACITY] =
    [const { AtomicUsize::new(0) }; DEVICE_RANGE_CAPACITY];
static KERNEL_TEXT_START: AtomicUsize = AtomicUsize::new(0);
static KERNEL_TEXT_END: AtomicUsize = AtomicUsize::new(0);
static KERNEL_RODATA_START: AtomicUsize = AtomicUsize::new(0);
static KERNEL_RODATA_END: AtomicUsize = AtomicUsize::new(0);

const USER_DATA_RW_XN: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_1)
    .union(El1Attributes::INNER_SHAREABLE)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::NON_GLOBAL)
    .union(El1Attributes::USER)
    .union(El1Attributes::PXN)
    .union(El1Attributes::UXN);

const USER_CODE_RO_X: El1Attributes = El1Attributes::VALID
    .union(El1Attributes::ATTRIBUTE_INDEX_1)
    .union(El1Attributes::INNER_SHAREABLE)
    .union(El1Attributes::READ_ONLY)
    .union(El1Attributes::ACCESSED)
    .union(El1Attributes::NON_GLOBAL)
    .union(El1Attributes::USER)
    .union(El1Attributes::PXN);

fn allocate_address_space_asid() -> Option<usize> {
    for asid in 2..ASID_COUNT {
        let word = &ASID_BITMAP[asid / 32];
        let mask = 1u32 << (asid % 32);
        if word.fetch_or(mask, Ordering::AcqRel) & mask == 0 {
            return Some(asid);
        }
    }
    None
}

fn release_address_space_asid(asid: usize) {
    if asid < 2 || asid >= ASID_COUNT {
        return;
    }
    let word = &ASID_BITMAP[asid / 32];
    let mask = 1u32 << (asid % 32);
    word.fetch_and(!mask, Ordering::AcqRel);
}

fn encode_address_space_handle(index: usize, generation: u32) -> u32 {
    ((generation & ADDRESS_SPACE_HANDLE_GENERATION_MASK) << 8) | (index as u32 + 1)
}

fn decode_address_space_handle(handle: u32) -> Option<(usize, u32)> {
    let encoded_index = (handle & ADDRESS_SPACE_HANDLE_INDEX_MASK) as usize;
    let generation = handle >> 8;
    if encoded_index == 0 || encoded_index > ADDRESS_SPACE_CAPACITY || generation == 0 {
        return None;
    }
    Some((encoded_index - 1, generation))
}

fn with_address_space_mut<R>(
    handle: u32,
    operation: impl FnOnce(&mut KernelMapping) -> R,
) -> Result<R, i32> {
    let Some((index, generation)) = decode_address_space_handle(handle) else {
        return Err(-1);
    };
    let slot = &ADDRESS_SPACES[index];
    if slot.generation.load(Ordering::Acquire) != generation {
        return Err(-1);
    }
    if slot.state.compare_exchange(
        ADDRESS_SPACE_READY,
        ADDRESS_SPACE_BUSY,
        Ordering::AcqRel,
        Ordering::Acquire,
    ).is_err() {
        return Err(-2);
    }

    // SAFETY: READY -> BUSY grants this caller exclusive access to the slot.
    let mapping = unsafe { (&mut *slot.mapping.get()).assume_init_mut() };
    let result = operation(mapping);
    slot.state.store(ADDRESS_SPACE_READY, Ordering::Release);
    Ok(result)
}

fn map_kernel_identity_into_space(mapping: &mut KernelMapping) -> Result<(), i32> {
    let ram_count = KERNEL_RAM_REGION_COUNT.load(Ordering::Acquire);
    if ram_count == 0 || ram_count > PAGE_RANGE_CAPACITY {
        return Err(-1);
    }

    for index in 0..ram_count {
        let base = KERNEL_RAM_BASES[index].load(Ordering::Relaxed);
        let size = KERNEL_RAM_LENGTHS[index].load(Ordering::Relaxed);
        map_identity_range(mapping, base, size, NORMAL_RW_XN).map_err(|_| -2)?;
    }

    let device_count = KERNEL_DEVICE_REGION_COUNT.load(Ordering::Acquire);
    if device_count == 0 || device_count > DEVICE_RANGE_CAPACITY {
        return Err(-3);
    }
    for index in 0..device_count {
        let base = KERNEL_DEVICE_BASES[index].load(Ordering::Relaxed);
        let size = KERNEL_DEVICE_LENGTHS[index].load(Ordering::Relaxed);
        map_identity_range(mapping, base, size, DEVICE_XN).map_err(|_| -4)?;
    }

    let text_start = KERNEL_TEXT_START.load(Ordering::Relaxed);
    let text_end = KERNEL_TEXT_END.load(Ordering::Relaxed);
    let rodata_start = KERNEL_RODATA_START.load(Ordering::Relaxed);
    let rodata_end = KERNEL_RODATA_END.load(Ordering::Relaxed);
    if map_identity_range(mapping, text_start, text_end - text_start, NORMAL_CODE_RO).is_err() {
        return Err(-5);
    }
    if map_identity_range(mapping, rodata_start, rodata_end - rodata_start, NORMAL_RO_XN).is_err() {
        return Err(-6);
    }
    Ok(())
}

fn create_address_space_mapping(asid: usize) -> Result<KernelMapping, i32> {
    // TTBR0 uses the lower 48-bit VA range; TTBR1 remains the shared kernel map.
    let mut mapping = Mapping::with_asid_and_va_range(
        KernelTranslation::new(),
        asid,
        0,
        El1And0,
        VaRange::Lower,
    );
    map_kernel_identity_into_space(&mut mapping)?;
    Ok(mapping)
}

fn user_virtual_page_is_safe(virtual_address: usize) -> bool {
    let Some(end) = virtual_address.checked_add(PAGE_BYTES) else {
        return false;
    };
    if virtual_address < USER_VA_START || end > USER_VA_LIMIT {
        return false;
    }

    // Do not let a process replace any low identity RAM or Device MMIO mapping
    // that the EL1 kernel relies on while it is running.
    let ram_count = KERNEL_RAM_REGION_COUNT.load(Ordering::Acquire);
    for index in 0..ram_count {
        let base = KERNEL_RAM_BASES[index].load(Ordering::Relaxed);
        let length = KERNEL_RAM_LENGTHS[index].load(Ordering::Relaxed);
        let Some(region_end) = base.checked_add(length) else { return false; };
        if virtual_address < region_end && base < end {
            return false;
        }
    }

    let device_count = KERNEL_DEVICE_REGION_COUNT.load(Ordering::Acquire);
    for index in 0..device_count {
        let base = KERNEL_DEVICE_BASES[index].load(Ordering::Relaxed);
        let length = KERNEL_DEVICE_LENGTHS[index].load(Ordering::Relaxed);
        let Some(region_end) = base.checked_add(length) else { return false; };
        if virtual_address < region_end && base < end {
            return false;
        }
    }
    true
}

fn address_space_page_matches(
    mapping: &KernelMapping,
    virtual_address: usize,
    physical_address: usize,
    executable_read_only: bool,
) -> bool {
    let Some(end) = virtual_address.checked_add(PAGE_BYTES) else {
        return false;
    };
    let range = MemoryRegion::new(virtual_address, end);
    let mut matched = false;
    let result = mapping.walk_range(&range, &mut |_, descriptor, level| {
        if level == 3 && descriptor.is_valid() && descriptor.is_table_or_page() {
            let flags = descriptor.flags();
            let base_flags = El1Attributes::USER
                .union(El1Attributes::NON_GLOBAL)
                .union(El1Attributes::ACCESSED);
            let permission_flags = if executable_read_only {
                El1Attributes::READ_ONLY.union(El1Attributes::PXN)
            } else {
                El1Attributes::PXN.union(El1Attributes::UXN)
            };
            let forbidden_flags = if executable_read_only {
                El1Attributes::UXN
            } else {
                El1Attributes::READ_ONLY
            };
            matched = descriptor.output_address().0 == physical_address
                && flags.contains(base_flags.union(permission_flags))
                && !flags.contains(forbidden_flags);
        }
        Ok(())
    });
    result.is_ok() && matched
}

fn address_space_page_is_mapped(mapping: &KernelMapping, virtual_address: usize) -> bool {
    let Some(end) = virtual_address.checked_add(PAGE_BYTES) else {
        return false;
    };
    let range = MemoryRegion::new(virtual_address, end);
    let mut mapped = false;
    let result = mapping.walk_range(&range, &mut |_, descriptor, level| {
        if level == 3 && descriptor.is_valid() && descriptor.is_table_or_page() {
            mapped = true;
        }
        Ok(())
    });
    result.is_ok() && mapped
}

#[no_mangle]
pub extern "C" fn fahim_address_space_create() -> u32 {
    if !MMU_INITIALIZED.load(Ordering::Acquire) {
        return u32::MAX;
    }

    for (index, slot) in ADDRESS_SPACES.iter().enumerate() {
        if slot.state.compare_exchange(
            ADDRESS_SPACE_FREE,
            ADDRESS_SPACE_BUILDING,
            Ordering::AcqRel,
            Ordering::Acquire,
        ).is_err() {
            continue;
        }

        let Some(asid) = allocate_address_space_asid() else {
            slot.state.store(ADDRESS_SPACE_FREE, Ordering::Release);
            return u32::MAX;
        };
        let mapping = match create_address_space_mapping(asid) {
            Ok(mapping) => mapping,
            Err(_) => {
                release_address_space_asid(asid);
                slot.state.store(ADDRESS_SPACE_FREE, Ordering::Release);
                return u32::MAX;
            }
        };

        let mut generation = slot.generation.load(Ordering::Relaxed)
            .wrapping_add(1) & ADDRESS_SPACE_HANDLE_GENERATION_MASK;
        if generation == 0 {
            generation = 1;
        }
        slot.generation.store(generation, Ordering::Relaxed);
        slot.asid.store(asid, Ordering::Relaxed);
        // SAFETY: BUILDING reserves the slot and the previous mapping was dropped.
        unsafe { (*slot.mapping.get()).write(mapping); }
        slot.state.store(ADDRESS_SPACE_READY, Ordering::Release);
        return encode_address_space_handle(index, generation);
    }
    u32::MAX
}

#[no_mangle]
pub extern "C" fn fahim_address_space_asid(handle: u32) -> u32 {
    let Some((index, generation)) = decode_address_space_handle(handle) else {
        return u32::MAX;
    };
    let slot = &ADDRESS_SPACES[index];
    if slot.generation.load(Ordering::Acquire) != generation
        || slot.state.load(Ordering::Acquire) != ADDRESS_SPACE_READY
    {
        return u32::MAX;
    }
    slot.asid.load(Ordering::Acquire) as u32
}

/// Map one caller-owned, allocated physical page into a process EL0 range.
/// permissions: 0 = user RW/XN data, 1 = user RO/X code.
#[no_mangle]
pub extern "C" fn fahim_address_space_map_user_page(
    handle: u32,
    virtual_address: u64,
    physical_address: u64,
    permissions: u32,
) -> i32 {
    if !MMU_INITIALIZED.load(Ordering::Acquire) {
        return -1;
    }
    let (Ok(virtual_address), Ok(physical_address)) = (
        usize::try_from(virtual_address),
        usize::try_from(physical_address),
    ) else {
        return -2;
    };
    if virtual_address & PAGE_MASK != 0
        || physical_address & PAGE_MASK != 0
        || !user_virtual_page_is_safe(virtual_address)
        || !page_is_usable(physical_address, PAGE_BYTES)
    {
        return -3;
    }
    let attributes = match permissions {
        0 => USER_DATA_RW_XN,
        1 => USER_CODE_RO_X,
        _ => return -4,
    };
    let end = virtual_address + PAGE_BYTES;
    match with_address_space_mut(handle, |mapping| {
        mapping.map_range(
            &MemoryRegion::new(virtual_address, end),
            PhysicalAddress(physical_address),
            attributes,
            Constraints::empty(),
        ).is_ok()
    }) {
        Ok(true) => 0,
        Ok(false) => -5,
        Err(error) => error,
    }
}

#[no_mangle]
pub extern "C" fn fahim_address_space_unmap_user_page(
    handle: u32,
    virtual_address: u64,
) -> i32 {
    let Ok(virtual_address) = usize::try_from(virtual_address) else {
        return -1;
    };
    if virtual_address & PAGE_MASK != 0 || !user_virtual_page_is_safe(virtual_address) {
        return -2;
    }
    let end = virtual_address + PAGE_BYTES;
    match with_address_space_mut(handle, |mapping| {
        if !address_space_page_is_mapped(mapping, virtual_address) {
            return false;
        }
        if mapping.map_range(
            &MemoryRegion::new(virtual_address, end),
            PhysicalAddress(0),
            El1Attributes::empty(),
            Constraints::empty(),
        ).is_err() {
            return false;
        }
        mapping.compact_subtables();
        true
    }) {
        Ok(true) => 0,
        Ok(false) => -3,
        Err(error) => error,
    }
}

#[no_mangle]
pub extern "C" fn fahim_address_space_destroy(handle: u32) -> i32 {
    let Some((index, generation)) = decode_address_space_handle(handle) else {
        return -1;
    };
    let slot = &ADDRESS_SPACES[index];
    if slot.generation.load(Ordering::Acquire) != generation {
        return -1;
    }
    if slot.state.compare_exchange(
        ADDRESS_SPACE_READY,
        ADDRESS_SPACE_DESTROYING,
        Ordering::AcqRel,
        Ordering::Acquire,
    ).is_err() {
        return -2;
    }

    // Never drop a translation while a CPU is using its TTBR0.
    let active = unsafe { (&*slot.mapping.get()).assume_init_ref().active() };
    if active {
        slot.state.store(ADDRESS_SPACE_READY, Ordering::Release);
        return -3;
    }
    let asid = slot.asid.load(Ordering::Relaxed);
    // SAFETY: DESTROYING gives this caller exclusive access; the mapping is inactive.
    unsafe { (&mut *slot.mapping.get()).assume_init_drop(); }
    release_address_space_asid(asid);
    slot.asid.store(0, Ordering::Relaxed);
    slot.state.store(ADDRESS_SPACE_FREE, Ordering::Release);
    0
}

/// QEMU coverage for separate ASIDs, EL0 permission flags, independent
/// mappings at the same VA, unmap/remap and page-table destruction.
#[no_mangle]
pub extern "C" fn fahim_address_space_self_test() -> i32 {
    if !MMU_INITIALIZED.load(Ordering::Acquire) {
        return -1;
    }

    let space_a = fahim_address_space_create();
    if space_a == u32::MAX {
        return -2;
    }
    let space_b = fahim_address_space_create();
    if space_b == u32::MAX {
        let _ = fahim_address_space_destroy(space_a);
        return -3;
    }

    let page_a = fahim_page_allocator_alloc(1);
    let page_b = fahim_page_allocator_alloc(1);
    if page_a == u64::MAX || page_b == u64::MAX {
        let _ = fahim_address_space_destroy(space_a);
        let _ = fahim_address_space_destroy(space_b);
        if page_a != u64::MAX {
            // SAFETY: page_a is the exact live one-page allocation above.
            unsafe { let _ = fahim_page_allocator_free(page_a, 1); }
        }
        if page_b != u64::MAX {
            // SAFETY: page_b is the exact live one-page allocation above.
            unsafe { let _ = fahim_page_allocator_free(page_b, 1); }
        }
        return -4;
    }

    const TEST_VA: u64 = (USER_VA_START as u64) + 0x20_0000;
    let asids_are_distinct = fahim_address_space_asid(space_a) != u32::MAX
        && fahim_address_space_asid(space_b) != u32::MAX
        && fahim_address_space_asid(space_a) != fahim_address_space_asid(space_b);
    let code_map_ok =
        fahim_address_space_map_user_page(space_a, TEST_VA, page_a, 1) == 0;
    let data_map_ok =
        fahim_address_space_map_user_page(space_b, TEST_VA, page_b, 0) == 0;
    let duplicate_rejected =
        fahim_address_space_map_user_page(space_a, TEST_VA, page_b, 0) != 0;

    let space_a_code_ok = with_address_space_mut(space_a, |mapping| {
        address_space_page_matches(mapping, TEST_VA as usize, page_a as usize, true)
    }).unwrap_or(false);
    let space_b_data_ok = with_address_space_mut(space_b, |mapping| {
        address_space_page_matches(mapping, TEST_VA as usize, page_b as usize, false)
    }).unwrap_or(false);

    let unmap_a_ok = fahim_address_space_unmap_user_page(space_a, TEST_VA) == 0;
    let space_a_empty = with_address_space_mut(space_a, |mapping| {
        !address_space_page_is_mapped(mapping, TEST_VA as usize)
    }).unwrap_or(false);
    let space_b_still_mapped = with_address_space_mut(space_b, |mapping| {
        address_space_page_matches(mapping, TEST_VA as usize, page_b as usize, false)
    }).unwrap_or(false);

    let remap_a_ok =
        fahim_address_space_map_user_page(space_a, TEST_VA, page_a, 0) == 0;
    let remap_a_verified = with_address_space_mut(space_a, |mapping| {
        address_space_page_matches(mapping, TEST_VA as usize, page_a as usize, false)
    }).unwrap_or(false);
    let unmap_b_ok = fahim_address_space_unmap_user_page(space_b, TEST_VA) == 0;
    let unmap_remap_a_ok = fahim_address_space_unmap_user_page(space_a, TEST_VA) == 0;

    let destroy_a_ok = fahim_address_space_destroy(space_a) == 0;
    let destroy_b_ok = fahim_address_space_destroy(space_b) == 0;
    // SAFETY: both pages are the exact live allocations returned above. Page
    // table destruction does not take ownership of the mapped data frames.
    let free_a_ok = unsafe { fahim_page_allocator_free(page_a, 1) } == 0;
    let free_b_ok = unsafe { fahim_page_allocator_free(page_b, 1) } == 0;

    // Keep failures distinguishable in the QEMU serial log.
    if !asids_are_distinct { return -10; }
    if !code_map_ok { return -11; }
    if !data_map_ok { return -12; }
    if !duplicate_rejected { return -13; }
    if !space_a_code_ok { return -14; }
    if !space_b_data_ok { return -15; }
    if !unmap_a_ok { return -16; }
    if !space_a_empty { return -17; }
    if !space_b_still_mapped { return -18; }
    if !remap_a_ok { return -19; }
    if !remap_a_verified { return -20; }
    if !unmap_b_ok { return -21; }
    if !unmap_remap_a_ok { return -22; }
    if !destroy_a_ok { return -23; }
    if !destroy_b_ok { return -24; }
    if !free_a_ok { return -25; }
    if !free_b_ok { return -26; }
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("wfe", options(nomem, nostack)) };
    }
}
