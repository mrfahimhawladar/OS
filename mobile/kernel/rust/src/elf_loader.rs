//! Bounded, no-heap AArch64 ELF64 ET_EXEC loader.
//!
//! ELF structures and constants are reused from the upstream MIT-licensed
//! Goblin crate. Fahim OS supplies address-space, physical-frame and
//! permission integration for its single-core EL0 execution model.

use core::mem::size_of;
use core::ptr;

use goblin::elf64::{header as elf_header, program_header as elf_program_header};

const MAX_ELF_FILE_BYTES: usize = 4 * 1024 * 1024;
const MAX_ELF_SEGMENTS: usize = 8;
const MAX_ELF_PROGRAM_HEADERS: usize = 32;
const MAX_ELF_TOTAL_PAGES: usize = 512;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FahimElfLoadedSegment {
    pub virtual_base: u64,
    pub physical_base: u64,
    pub page_count: u32,
    pub permissions: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FahimElfLoadedImage {
    pub entry: u64,
    pub address_space: u32,
    pub segment_count: u32,
    pub segments: [FahimElfLoadedSegment; MAX_ELF_SEGMENTS],
}

impl FahimElfLoadedImage {
    const fn empty() -> Self {
        Self {
            entry: 0,
            address_space: u32::MAX,
            segment_count: 0,
            segments: [FahimElfLoadedSegment {
                virtual_base: 0,
                physical_base: 0,
                page_count: 0,
                permissions: 0,
            }; MAX_ELF_SEGMENTS],
        }
    }
}

#[derive(Clone, Copy, Default)]
struct ParsedSegment {
    virtual_base: usize,
    page_base: usize,
    file_offset: usize,
    file_size: usize,
    memory_size: usize,
    page_count: usize,
    page_offset: usize,
    permissions: u32,
}

fn parse_elf64(image: &[u8]) -> Result<(u64, [ParsedSegment; MAX_ELF_SEGMENTS], usize), i32> {
    let header_size = size_of::<elf_header::Header>();
    let program_header_size = size_of::<elf_program_header::ProgramHeader>();
    if image.len() < header_size || image.len() > MAX_ELF_FILE_BYTES {
        return Err(-1);
    }

    // Goblin exposes repr(C), Plain ELF structures. read_unaligned permits
    // parsing an ELF image embedded at any byte alignment.
    let header = unsafe {
        ptr::read_unaligned(image.as_ptr().cast::<elf_header::Header>())
    };

    if &header.e_ident[0..elf_header::SELFMAG] != elf_header::ELFMAG ||
        header.e_ident[elf_header::EI_CLASS] != elf_header::ELFCLASS64 ||
        header.e_ident[elf_header::EI_DATA] != elf_header::ELFDATA2LSB ||
        header.e_ident[elf_header::EI_VERSION] != elf_header::EV_CURRENT ||
        header.e_type != elf_header::ET_EXEC ||
        header.e_machine != elf_header::EM_AARCH64 ||
        header.e_version != elf_header::EV_CURRENT as u32 ||
        header.e_ehsize as usize != header_size ||
        header.e_phentsize as usize != program_header_size ||
        header.e_phnum == 0 ||
        header.e_phnum as usize > MAX_ELF_PROGRAM_HEADERS {
        return Err(-2);
    }

    let program_offset = usize::try_from(header.e_phoff).map_err(|_| -3)?;
    let program_bytes = (header.e_phnum as usize)
        .checked_mul(program_header_size)
        .ok_or(-3)?;
    let program_end = program_offset.checked_add(program_bytes).ok_or(-3)?;
    if program_offset < header_size || program_end > image.len() {
        return Err(-3);
    }

    let mut segments = [ParsedSegment::default(); MAX_ELF_SEGMENTS];
    let mut segment_count = 0usize;
    let mut total_pages = 0usize;
    let mut entry_is_executable = false;

    for index in 0..header.e_phnum as usize {
        let offset = program_offset + index * program_header_size;
        let ph = unsafe {
            ptr::read_unaligned(
                image.as_ptr().add(offset).cast::<elf_program_header::ProgramHeader>()
            )
        };

        match ph.p_type {
            elf_program_header::PT_INTERP |
            elf_program_header::PT_DYNAMIC |
            elf_program_header::PT_TLS => return Err(-4),
            elf_program_header::PT_LOAD => {}
            _ => continue,
        }

        if ph.p_memsz == 0 {
            if ph.p_filesz != 0 {
                return Err(-5);
            }
            continue;
        }
        if segment_count >= MAX_ELF_SEGMENTS ||
            ph.p_filesz > ph.p_memsz ||
            (ph.p_flags & elf_program_header::PF_R) == 0 ||
            (ph.p_flags & elf_program_header::PF_W != 0 &&
             ph.p_flags & elf_program_header::PF_X != 0) {
            return Err(-5);
        }

        let file_offset = usize::try_from(ph.p_offset).map_err(|_| -6)?;
        let file_size = usize::try_from(ph.p_filesz).map_err(|_| -6)?;
        let memory_size = usize::try_from(ph.p_memsz).map_err(|_| -6)?;
        let virtual_base = usize::try_from(ph.p_vaddr).map_err(|_| -6)?;
        let file_end = file_offset.checked_add(file_size).ok_or(-6)?;
        if file_end > image.len() {
            return Err(-6);
        }

        let memory_end = virtual_base.checked_add(memory_size).ok_or(-7)?;
        if virtual_base < crate::USER_VA_START ||
            memory_end > crate::USER_VA_LIMIT ||
            memory_end <= virtual_base {
            return Err(-7);
        }

        let page_offset = virtual_base & crate::PAGE_MASK;
        if (file_offset & crate::PAGE_MASK) != page_offset {
            return Err(-8);
        }

        if ph.p_align > 1 {
            let alignment = usize::try_from(ph.p_align).map_err(|_| -8)?;
            if !alignment.is_power_of_two() ||
                virtual_base % alignment != file_offset % alignment {
                return Err(-8);
            }
        }

        let span = page_offset.checked_add(memory_size).ok_or(-9)?;
        let rounded = span.checked_add(crate::PAGE_MASK).ok_or(-9)?
            & !crate::PAGE_MASK;
        let page_count = rounded / crate::PAGE_BYTES;
        if page_count == 0 ||
            page_count > MAX_ELF_TOTAL_PAGES ||
            total_pages.checked_add(page_count).ok_or(-9)? > MAX_ELF_TOTAL_PAGES {
            return Err(-9);
        }

        let page_base = virtual_base & !crate::PAGE_MASK;
        let page_bytes = page_count.checked_mul(crate::PAGE_BYTES).ok_or(-9)?;
        let page_end = page_base.checked_add(page_bytes).ok_or(-9)?;
        if page_base < crate::USER_VA_START || page_end > crate::USER_VA_LIMIT {
            return Err(-9);
        }

        // Reject page-rounded overlap instead of replacing an earlier PTE.
        for previous in segments.iter().take(segment_count) {
            let previous_bytes = previous.page_count.checked_mul(crate::PAGE_BYTES)
                .ok_or(-10)?;
            let previous_end = previous.page_base.checked_add(previous_bytes)
                .ok_or(-10)?;
            if page_base < previous_end && previous.page_base < page_end {
                return Err(-10);
            }
        }

        let writable = ph.p_flags & elf_program_header::PF_W != 0;
        let executable = ph.p_flags & elf_program_header::PF_X != 0;
        let permissions = if executable {
            crate::FAHIM_USER_PAGE_RO_X
        } else if writable {
            crate::FAHIM_USER_PAGE_RW_XN
        } else {
            crate::FAHIM_USER_PAGE_RO_XN
        };

        if executable &&
            (header.e_entry as usize) >= virtual_base &&
            (header.e_entry as usize) < memory_end {
            entry_is_executable = true;
        }

        segments[segment_count] = ParsedSegment {
            virtual_base,
            page_base,
            file_offset,
            file_size,
            memory_size,
            page_count,
            page_offset,
            permissions,
        };
        segment_count += 1;
        total_pages += page_count;
    }

    if segment_count == 0 ||
        !entry_is_executable ||
        header.e_entry & 3 != 0 {
        return Err(-11);
    }

    Ok((header.e_entry, segments, segment_count))
}

fn rollback_loaded_segments(
    address_space: u32,
    loaded: &FahimElfLoadedImage,
    mapped_pages: &[u32; MAX_ELF_SEGMENTS],
) {
    for index in 0..loaded.segment_count as usize {
        let segment = loaded.segments[index];
        let page_count = segment.page_count as usize;
        let base = segment.virtual_base;
        let mut safe_to_free = true;

        for page in 0..mapped_pages[index] as usize {
            let virtual_address = base + (page * crate::PAGE_BYTES) as u64;
            if crate::fahim_address_space_unmap_user_page(
                address_space, virtual_address
            ) != 0 {
                safe_to_free = false;
            }
        }

        // If any PTE might still reference this block, leak it safely rather
        // than returning a potentially mapped frame to the allocator.
        if safe_to_free {
            unsafe {
                let _ = crate::fahim_page_allocator_free(
                    segment.physical_base, page_count
                );
            }
        }
    }
}

/// Load a little-endian AArch64 ET_EXEC image into an inactive EL0 address space.
///
/// Supports up to eight disjoint PT_LOAD segments and 512 mapped pages.
/// PT_INTERP, PT_DYNAMIC, PT_TLS, W+X segments, ET_DYN/PIE and page-rounded
/// overlapping PT_LOAD segments are rejected. The caller owns the returned
/// physical segment allocations and must release them only after the
/// associated address space has been destroyed.
///
/// # Safety
/// image must point to image_length readable kernel bytes and output must point
/// to a writable FahimElfLoadedImage. The address space must exist and remain
/// inactive for the duration of this call.
#[no_mangle]
pub unsafe extern "C" fn fahim_elf64_load_user_image(
    address_space: u32,
    image: *const u8,
    image_length: usize,
    output: *mut FahimElfLoadedImage,
) -> i32 {
    if image.is_null() || output.is_null() ||
        image_length == 0 || image_length > MAX_ELF_FILE_BYTES ||
        address_space == u32::MAX {
        return -1;
    }
    unsafe { output.write(FahimElfLoadedImage::empty()) };

    if crate::ACTIVE_ADDRESS_SPACE_HANDLE.load(core::sync::atomic::Ordering::Acquire)
        == address_space {
        return -2;
    }
    if let Some((index, generation)) = crate::decode_address_space_handle(address_space) {
        let slot = &crate::ADDRESS_SPACES[index];
        if slot.generation.load(core::sync::atomic::Ordering::Acquire) != generation ||
            slot.state.load(core::sync::atomic::Ordering::Acquire) != crate::ADDRESS_SPACE_READY {
            return -2;
        }
    } else {
        return -2;
    }

    let image_bytes = unsafe { core::slice::from_raw_parts(image, image_length) };
    let (entry, parsed, parsed_count) = match parse_elf64(image_bytes) {
        Ok(parsed) => parsed,
        Err(error) => return error,
    };

    let mut loaded = FahimElfLoadedImage::empty();
    loaded.entry = entry;
    loaded.address_space = address_space;
    let mut mapped_pages = [0u32; MAX_ELF_SEGMENTS];

    for index in 0..parsed_count {
        let segment = parsed[index];
        let page_count = segment.page_count;
        let physical_base = crate::fahim_page_allocator_alloc(page_count);
        if physical_base == u64::MAX {
            rollback_loaded_segments(address_space, &loaded, &mapped_pages);
            return -12;
        }

        let total_bytes = match page_count.checked_mul(crate::PAGE_BYTES) {
            Some(value) => value,
            None => {
                unsafe { let _ = crate::fahim_page_allocator_free(physical_base, page_count); }
                rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                return -13;
            }
        };
        let physical_base_usize = match usize::try_from(physical_base) {
            Ok(value) => value,
            Err(_) => {
                unsafe { let _ = crate::fahim_page_allocator_free(physical_base, page_count); }
                rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                return -13;
            }
        };
        let alias = match crate::HIGH_HALF_DIRECT_MAP_BASE.checked_add(physical_base_usize) {
            Some(value) => value,
            None => {
                unsafe { let _ = crate::fahim_page_allocator_free(physical_base, page_count); }
                rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                return -13;
            }
        };

        unsafe {
            core::ptr::write_bytes(alias as *mut u8, 0, total_bytes);
            if segment.file_size != 0 {
                core::ptr::copy_nonoverlapping(
                    image_bytes.as_ptr().add(segment.file_offset),
                    (alias + segment.page_offset) as *mut u8,
                    segment.file_size,
                );
            }
        }

        loaded.segments[index] = FahimElfLoadedSegment {
            virtual_base: segment.page_base as u64,
            physical_base,
            page_count: page_count as u32,
            permissions: segment.permissions,
        };
        loaded.segment_count = (index + 1) as u32;

        for page in 0..page_count {
            let virtual_address = match segment.page_base.checked_add(page * crate::PAGE_BYTES) {
                Some(value) => value,
                None => {
                    rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                    return -14;
                }
            };
            let physical_address = match physical_base_usize.checked_add(page * crate::PAGE_BYTES) {
                Some(value) => value,
                None => {
                    rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                    return -14;
                }
            };

            let result = crate::fahim_address_space_map_user_page(
                address_space,
                virtual_address as u64,
                physical_address as u64,
                segment.permissions,
            );
            if result != 0 {
                rollback_loaded_segments(address_space, &loaded, &mapped_pages);
                return -15;
            }
            mapped_pages[index] += 1;
        }
    }

    unsafe { output.write(loaded) };
    0
}

/// Release segment frames returned by the ELF loader.
///
/// # Safety
/// image must be an unchanged loader result, and its address space must
/// already have been destroyed so no page table can reference these frames.
#[no_mangle]
pub unsafe extern "C" fn fahim_elf64_release_user_image(
    image: *mut FahimElfLoadedImage,
) -> i32 {
    if image.is_null() {
        return -1;
    }
    let loaded = unsafe { *image };
    if loaded.segment_count as usize > MAX_ELF_SEGMENTS ||
        loaded.address_space == u32::MAX ||
        crate::ACTIVE_ADDRESS_SPACE_HANDLE.load(core::sync::atomic::Ordering::Acquire)
            == loaded.address_space {
        return -2;
    }

    if let Some((index, generation)) = crate::decode_address_space_handle(loaded.address_space) {
        let slot = &crate::ADDRESS_SPACES[index];
        if slot.generation.load(core::sync::atomic::Ordering::Acquire) == generation &&
            slot.state.load(core::sync::atomic::Ordering::Acquire) != crate::ADDRESS_SPACE_FREE {
            return -3;
        }
    }

    for segment in loaded.segments.iter().take(loaded.segment_count as usize) {
        if segment.page_count == 0 || segment.physical_base == u64::MAX {
            return -4;
        }
        let result = unsafe {
            crate::fahim_page_allocator_free(
                segment.physical_base, segment.page_count as usize
            )
        };
        if result != 0 {
            return -5;
        }
    }

    unsafe { image.write(FahimElfLoadedImage::empty()) };
    0
}
