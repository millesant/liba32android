#include <cstdint>
#include <iostream>
#include <vector>

#include "elf/elf32_dynamic_placement.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DynamicPlacementError;
using liba32android::elf::Elf32DynamicPlacementOptions;
using liba32android::elf::Elf32LoadError;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::load_elf32;
using liba32android::elf::place_elf32_dynamic;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::size_t kHeaderSize = 52;
constexpr std::size_t kProgramHeaderSize = 32;
constexpr std::size_t kProgramHeaderOffset = kHeaderSize;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

void write_u16(std::vector<std::uint8_t>& image,
               std::size_t offset,
               std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& image,
               std::size_t offset,
               std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    image[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    image[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::vector<std::uint8_t> make_image(std::uint16_t type) {
    std::vector<std::uint8_t> image(0x200, 0);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    image[6] = 1;

    write_u16(image, 16, type);
    write_u16(image, 18, 40);
    write_u32(image, 20, 1);
    write_u32(image, 24, 0x80);
    write_u32(image, 28, kProgramHeaderOffset);
    write_u16(image, 40, kHeaderSize);
    write_u16(image, 42, kProgramHeaderSize);
    write_u16(image, 44, 1);

    write_u32(image, kProgramHeaderOffset + 0, 1);
    write_u32(image, kProgramHeaderOffset + 4, 0);
    write_u32(image, kProgramHeaderOffset + 8, 0);
    write_u32(image, kProgramHeaderOffset + 16, 0x100);
    write_u32(image, kProgramHeaderOffset + 20, 0x2100);
    write_u32(image, kProgramHeaderOffset + 24, 5);
    write_u32(image, kProgramHeaderOffset + 28, 0x4000);
    return image;
}

int test_first_fit_and_load() {
    MappedGuestMemory memory;
    auto image = make_image(3);

    Elf32DynamicPlacementOptions placement_options;
    placement_options.search_begin = 0x10000;
    placement_options.search_end_exclusive = 0x40000;

    const auto placement = place_elf32_dynamic(memory, image, placement_options);
    if (!placement || placement.dynamic_base != 0x10000 ||
        (placement.dynamic_base % 0x4000) != 0) {
        return fail("ET_DYN first-fit placement did not choose the expected aligned base");
    }
    if (memory.is_mapped(placement.dynamic_base)) {
        return fail("ET_DYN placement unexpectedly mapped its result");
    }

    Elf32LoadOptions load_options;
    load_options.dynamic_base = placement.dynamic_base;
    const auto loaded = load_elf32(memory, image, load_options);
    if (!loaded || loaded.load_bias != placement.dynamic_base ||
        loaded.entry != placement.dynamic_base + 0x80) {
        return fail("placed ET_DYN base was not accepted unchanged by the loader");
    }
    return 0;
}

int test_conflict_skip() {
    MappedGuestMemory memory;
    auto image = make_image(3);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;

    if (!memory.map(0x10000, memory.page_size(), rw)) {
        return fail("conflict-skip setup mapping failed");
    }

    Elf32DynamicPlacementOptions options;
    options.search_begin = 0x10000;
    options.search_end_exclusive = 0x40000;
    const auto placement = place_elf32_dynamic(memory, image, options);
    if (!placement || placement.dynamic_base != 0x14000) {
        return fail("ET_DYN placement did not skip the occupied first candidate");
    }
    if (!memory.is_mapped(0x10000) || memory.permissions(0x10000) != rw ||
        memory.is_mapped(placement.dynamic_base)) {
        return fail("ET_DYN placement mutated guest mappings");
    }
    return 0;
}

int test_bounded_no_space() {
    MappedGuestMemory memory;
    auto image = make_image(3);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;

    if (!memory.map(0x10000, memory.page_size(), rw)) {
        return fail("no-space setup mapping failed");
    }

    Elf32DynamicPlacementOptions options;
    options.search_begin = 0x10000;
    options.search_end_exclusive = 0x14000;
    const auto placement = place_elf32_dynamic(memory, image, options);
    if (placement || placement.error != Elf32DynamicPlacementError::NoSpace) {
        return fail("bounded occupied ET_DYN window did not return no-space");
    }
    if (!memory.is_mapped(0x10000) || memory.permissions(0x10000) != rw) {
        return fail("failed ET_DYN placement changed existing memory");
    }
    return 0;
}

int test_invalid_image_and_exec() {
    MappedGuestMemory memory;

    auto malformed = make_image(3);
    malformed[0] = 0;
    const auto malformed_result = place_elf32_dynamic(memory, malformed);
    if (malformed_result ||
        malformed_result.error != Elf32DynamicPlacementError::InvalidImage ||
        malformed_result.load_error != Elf32LoadError::BadMagic) {
        return fail("malformed ELF was not reported through placement");
    }

    const auto executable = place_elf32_dynamic(memory, make_image(2));
    if (executable || executable.error != Elf32DynamicPlacementError::NotDynamic) {
        return fail("ET_EXEC image was not rejected by dynamic placement");
    }
    return 0;
}

int test_invalid_window() {
    MappedGuestMemory memory;
    auto image = make_image(3);

    Elf32DynamicPlacementOptions options;
    options.search_begin = 0x20000;
    options.search_end_exclusive = 0x20000;
    const auto result = place_elf32_dynamic(memory, image, options);
    if (result || result.error != Elf32DynamicPlacementError::InvalidSearchWindow) {
        return fail("invalid placement search window was not rejected");
    }
    return 0;
}

}  // namespace

int main() {
    if (test_first_fit_and_load() != 0) return 1;
    if (test_conflict_skip() != 0) return 1;
    if (test_bounded_no_space() != 0) return 1;
    if (test_invalid_image_and_exec() != 0) return 1;
    if (test_invalid_window() != 0) return 1;
    return 0;
}
