#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32LoadError;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::load_elf32;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::size_t kHeaderSize = 52;
constexpr std::size_t kProgramHeaderSize = 32;
constexpr std::size_t kProgramHeaderOffset = kHeaderSize;
constexpr std::size_t kFirstProgramHeader = kProgramHeaderOffset;
constexpr std::size_t kSecondProgramHeader = kProgramHeaderOffset + kProgramHeaderSize;
constexpr std::size_t kThirdProgramHeader = kProgramHeaderOffset + 2 * kProgramHeaderSize;
constexpr std::size_t kFourthProgramHeader = kProgramHeaderOffset + 3 * kProgramHeaderSize;
constexpr std::uint32_t kProgramTypeGnuRelro = 0x6474e552U;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

void write_u16(std::vector<std::uint8_t>& image, std::size_t offset, std::uint16_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& image, std::size_t offset, std::uint32_t value) {
    image[offset] = static_cast<std::uint8_t>(value & 0xffU);
    image[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    image[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    image[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::vector<std::uint8_t> make_image(std::uint16_t type, std::uint32_t virtual_base) {
    std::vector<std::uint8_t> image(0x1010, 0);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;  // ELFCLASS32
    image[5] = 1;  // ELFDATA2LSB
    image[6] = 1;  // EV_CURRENT

    write_u16(image, 16, type);
    write_u16(image, 18, 40);  // EM_ARM
    write_u32(image, 20, 1);   // EV_CURRENT
    write_u32(image, 24, virtual_base + 0x80);
    write_u32(image, 28, kProgramHeaderOffset);
    write_u16(image, 40, kHeaderSize);
    write_u16(image, 42, kProgramHeaderSize);
    write_u16(image, 44, 2);

    // PT_LOAD #0: file/header page, read+execute.
    write_u32(image, kFirstProgramHeader + 0, 1);  // PT_LOAD
    write_u32(image, kFirstProgramHeader + 4, 0);
    write_u32(image, kFirstProgramHeader + 8, virtual_base);
    write_u32(image, kFirstProgramHeader + 16, 0x100);
    write_u32(image, kFirstProgramHeader + 20, 0x100);
    write_u32(image, kFirstProgramHeader + 24, 5);  // PF_R | PF_X
    write_u32(image, kFirstProgramHeader + 28, 0x1000);

    // PT_LOAD #1: data plus BSS, read+write.
    write_u32(image, kSecondProgramHeader + 0, 1);  // PT_LOAD
    write_u32(image, kSecondProgramHeader + 4, 0x1000);
    write_u32(image, kSecondProgramHeader + 8, virtual_base + 0x2000);
    write_u32(image, kSecondProgramHeader + 16, 4);
    write_u32(image, kSecondProgramHeader + 20, 0x20);
    write_u32(image, kSecondProgramHeader + 24, 6);  // PF_R | PF_W
    write_u32(image, kSecondProgramHeader + 28, 0x1000);

    image[0x1000] = 0x78;
    image[0x1001] = 0x56;
    image[0x1002] = 0x34;
    image[0x1003] = 0x12;
    return image;
}

void add_dynamic_segment(std::vector<std::uint8_t>& image, std::uint32_t virtual_base) {
    write_u16(image, 44, 3);
    write_u32(image, kThirdProgramHeader + 0, 2);  // PT_DYNAMIC
    write_u32(image, kThirdProgramHeader + 4, 0xc0);
    write_u32(image, kThirdProgramHeader + 8, virtual_base + 0xc0);
    write_u32(image, kThirdProgramHeader + 16, 0x10);
    write_u32(image, kThirdProgramHeader + 20, 0x10);
    write_u32(image, kThirdProgramHeader + 24, 4);  // PF_R
    write_u32(image, kThirdProgramHeader + 28, 4);

    for (std::size_t i = 0; i < 0x10; ++i) {
        image[0xc0 + i] = static_cast<std::uint8_t>(0xa0 + i);
    }
}

void add_relro_segment(std::vector<std::uint8_t>& image,
                       std::uint32_t virtual_base) {
    write_u16(image, 44, 3);
    write_u32(image, kThirdProgramHeader + 0, kProgramTypeGnuRelro);
    write_u32(image, kThirdProgramHeader + 8, virtual_base + 0x2000);
    write_u32(image, kThirdProgramHeader + 20, 0x20);
    write_u32(image, kThirdProgramHeader + 24, 4);
    write_u32(image, kThirdProgramHeader + 28, 1);
}

bool all_zero(std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

int test_valid_dynamic() {
    auto image = make_image(3, 0);  // ET_DYN
    MappedGuestMemory memory;
    const std::uint32_t dynamic_base = static_cast<std::uint32_t>(memory.page_size() * 512);

    Elf32LoadOptions options;
    options.dynamic_base = dynamic_base;
    const auto result = load_elf32(memory, image, options);
    if (!result || result.load_bias != dynamic_base || result.entry != dynamic_base + 0x80 ||
        result.segments.size() != 2) {
        return fail("valid ET_DYN image did not load with the expected metadata");
    }
    if (result.dynamic_segment.has_value()) {
        return fail("missing PT_DYNAMIC unexpectedly produced dynamic metadata");
    }
    if (!result.relro_segments.empty()) {
        return fail("missing PT_GNU_RELRO unexpectedly produced RELRO metadata");
    }

    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (memory.permissions(dynamic_base) != rx ||
        memory.permissions(dynamic_base + 0x2000) != rw) {
        return fail("final PT_LOAD permissions do not match ELF flags");
    }

    std::array<std::uint8_t, 4> data{};
    if (!memory.read(dynamic_base + 0x2000, data) ||
        data != std::array<std::uint8_t, 4>{0x78, 0x56, 0x34, 0x12}) {
        return fail("PT_LOAD file bytes were not copied to the expected guest address");
    }

    std::array<std::uint8_t, 0x1c> bss{};
    bss.fill(0xff);
    if (!memory.read(dynamic_base + 0x2004, bss) || !all_zero(bss)) {
        return fail("PT_LOAD BSS tail was not zero-filled");
    }

    constexpr std::array<std::uint8_t, 1> byte{0xaa};
    if (memory.write(dynamic_base, byte)) {
        return fail("write unexpectedly succeeded on final RX segment");
    }
    std::array<std::uint8_t, 4> code{};
    if (!memory.read_code(dynamic_base, code)) {
        return fail("instruction fetch failed on final RX segment");
    }
    if (memory.read_code(dynamic_base + 0x2000, code)) {
        return fail("instruction fetch unexpectedly succeeded on final RW segment");
    }

    return 0;
}

int test_relro_metadata() {
    auto image = make_image(3, 0);
    add_relro_segment(image, 0);

    MappedGuestMemory memory;
    const std::uint32_t dynamic_base =
        static_cast<std::uint32_t>(memory.page_size() * 512);

    Elf32LoadOptions options;
    options.dynamic_base = dynamic_base;
    const auto result = load_elf32(memory, image, options);
    if (!result || result.relro_segments.size() != 1) {
        return fail("valid GNU RELRO metadata was not published by the loader");
    }

    const auto& relro = result.relro_segments.front();
    const std::uint64_t page = memory.page_size();
    const std::uint64_t expected_start =
        (static_cast<std::uint64_t>(dynamic_base + 0x2000) / page) * page;
    const std::uint64_t exact_end =
        static_cast<std::uint64_t>(dynamic_base + 0x2000) + 0x20;
    const std::uint64_t remainder = exact_end % page;
    const std::uint64_t expected_end =
        remainder == 0 ? exact_end : exact_end + (page - remainder);

    if (relro.guest_address != dynamic_base + 0x2000 ||
        relro.memory_size != 0x20 ||
        relro.mapping_start != expected_start ||
        relro.mapping_size != expected_end - expected_start) {
        return fail("load-biased GNU RELRO metadata was incorrect");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (memory.permissions(dynamic_base + 0x2000) != rw) {
        return fail("loader sealed GNU RELRO before relocation time");
    }

    constexpr std::array<std::uint8_t, 1> byte{0xaa};
    if (!memory.write(dynamic_base + 0x2000, byte)) {
        return fail("GNU RELRO page was not writable immediately after load");
    }
    return 0;
}

int test_valid_exec() {
    constexpr std::uint32_t fixed_base = 0x10000;
    auto image = make_image(2, fixed_base);  // ET_EXEC
    MappedGuestMemory memory;

    Elf32LoadOptions options;
    options.dynamic_base = 0x400000;  // Must be ignored for ET_EXEC.
    const auto result = load_elf32(memory, image, options);
    if (!result || result.load_bias != 0 || result.entry != fixed_base + 0x80) {
        return fail("valid ET_EXEC image did not retain fixed guest addresses");
    }
    if (!memory.is_mapped(fixed_base) || !memory.is_mapped(fixed_base + 0x2000)) {
        return fail("ET_EXEC PT_LOAD pages were not mapped at fixed guest addresses");
    }
    return 0;
}

int test_dynamic_metadata() {
    Elf32LoadOptions options;
    options.dynamic_base = 0x200000;

    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        MappedGuestMemory memory;
        const auto result = load_elf32(memory, image, options);
        if (!result || !result.dynamic_segment.has_value()) {
            return fail("valid PT_DYNAMIC metadata was not reported");
        }
        const auto& dynamic = *result.dynamic_segment;
        if (dynamic.guest_address != options.dynamic_base.value() + 0xc0 ||
            dynamic.file_size != 0x10 || dynamic.memory_size != 0x10) {
            return fail("PT_DYNAMIC metadata did not preserve biased guest VA/range");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u16(image, 44, 4);
        for (std::size_t i = 0; i < kProgramHeaderSize; ++i) {
            image[kFourthProgramHeader + i] = image[kThirdProgramHeader + i];
        }
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::MultipleDynamicSegments) {
            return fail("multiple PT_DYNAMIC segments were not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 20, 0);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::DynamicSegmentEmpty) {
            return fail("empty PT_DYNAMIC was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 16, 0x20);
        write_u32(image, kThirdProgramHeader + 20, 0x10);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error !=
            Elf32LoadError::DynamicSegmentFileszExceedsMemsz) {
            return fail("PT_DYNAMIC p_filesz > p_memsz was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 4, static_cast<std::uint32_t>(image.size()));
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error !=
            Elf32LoadError::DynamicSegmentFileOutOfBounds) {
            return fail("out-of-bounds PT_DYNAMIC file range was not rejected");
        }
    }
    {
        auto image = make_image(2, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 8, 0xfffffff8U);
        write_u32(image, kThirdProgramHeader + 20, 0x10);
        MappedGuestMemory memory;
        if (load_elf32(memory, image).error != Elf32LoadError::DynamicSegmentAddressOverflow) {
            return fail("PT_DYNAMIC guest-address overflow was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 8, 0x1000);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::DynamicSegmentOutsideLoad) {
            return fail("PT_DYNAMIC outside PT_LOAD memory was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kFirstProgramHeader + 24, 0);  // Supported but not readable.
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::DynamicSegmentNotReadable) {
            return fail("PT_DYNAMIC inside non-readable PT_LOAD was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        add_dynamic_segment(image, 0);
        write_u32(image, kThirdProgramHeader + 4, 0xd0);  // VA still maps file offset 0xc0.
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error !=
            Elf32LoadError::DynamicSegmentFileMappingMismatch) {
            return fail("PT_DYNAMIC file range inconsistent with PT_LOAD mapping was not rejected");
        }
        if (memory.is_mapped(options.dynamic_base.value())) {
            return fail("malformed PT_DYNAMIC mutated guest mappings before rejection");
        }
    }

    return 0;
}

int test_header_validation() {
    MappedGuestMemory memory;

    std::vector<std::uint8_t> truncated(16, 0);
    if (load_elf32(memory, truncated).error != Elf32LoadError::TruncatedHeader) {
        return fail("truncated ELF header was not rejected");
    }

    auto image = make_image(3, 0);
    image[0] = 0;
    if (load_elf32(memory, image).error != Elf32LoadError::BadMagic) {
        return fail("bad ELF magic was not rejected");
    }

    image = make_image(3, 0);
    image[4] = 2;
    if (load_elf32(memory, image).error != Elf32LoadError::UnsupportedClass) {
        return fail("non-ELF32 class was not rejected");
    }

    image = make_image(3, 0);
    image[5] = 2;
    if (load_elf32(memory, image).error != Elf32LoadError::UnsupportedEndian) {
        return fail("big-endian ELF was not rejected");
    }

    image = make_image(3, 0);
    write_u16(image, 16, 1);
    if (load_elf32(memory, image).error != Elf32LoadError::UnsupportedType) {
        return fail("unsupported ELF type was not rejected");
    }

    image = make_image(3, 0);
    write_u16(image, 18, 62);
    if (load_elf32(memory, image).error != Elf32LoadError::UnsupportedMachine) {
        return fail("non-ARM machine was not rejected");
    }

    image = make_image(3, 0);
    write_u16(image, 40, 0);
    if (load_elf32(memory, image).error != Elf32LoadError::InvalidHeaderSize) {
        return fail("invalid ELF32 header size was not rejected");
    }

    image = make_image(3, 0);
    write_u16(image, 42, 0);
    if (load_elf32(memory, image).error != Elf32LoadError::InvalidProgramHeaderSize) {
        return fail("invalid program-header size was not rejected");
    }

    image = make_image(3, 0);
    if (load_elf32(memory, image).error != Elf32LoadError::DynamicBaseRequired) {
        return fail("ET_DYN without an explicit guest base was not rejected");
    }

    Elf32LoadOptions options;
    options.dynamic_base = 0x200001;
    if (load_elf32(memory, image, options).error != Elf32LoadError::DynamicBaseUnaligned) {
        return fail("unaligned ET_DYN base was not rejected");
    }

    return 0;
}

int test_program_header_bounds() {
    auto image = make_image(3, 0);
    write_u32(image, 28, static_cast<std::uint32_t>(image.size() - 16));
    MappedGuestMemory memory;
    if (load_elf32(memory, image).error != Elf32LoadError::ProgramHeaderTableOutOfBounds) {
        return fail("out-of-bounds program-header table was not rejected");
    }
    return 0;
}

int test_segment_validation() {
    Elf32LoadOptions options;
    options.dynamic_base = 0x200000;

    {
        auto image = make_image(3, 0);
        write_u32(image, kSecondProgramHeader + 16, 0x40);
        write_u32(image, kSecondProgramHeader + 20, 0x20);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::SegmentFileszExceedsMemsz) {
            return fail("p_filesz > p_memsz was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        write_u32(image, kSecondProgramHeader + 4, static_cast<std::uint32_t>(image.size()));
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::SegmentFileOutOfBounds) {
            return fail("out-of-bounds PT_LOAD file range was not rejected");
        }
    }
    {
        auto image = make_image(2, 0);
        write_u32(image, kSecondProgramHeader + 8, 0xfffff000U);
        write_u32(image, kSecondProgramHeader + 20, 0x2000);
        MappedGuestMemory memory;
        if (load_elf32(memory, image).error != Elf32LoadError::SegmentAddressOverflow) {
            return fail("PT_LOAD guest-address overflow was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        write_u32(image, kSecondProgramHeader + 8, 0x2100);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::SegmentAlignmentInvalid) {
            return fail("invalid p_vaddr/p_offset alignment was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        write_u32(image, kSecondProgramHeader + 24, 7);  // PF_R | PF_W | PF_X
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::UnsupportedSegmentPermissions) {
            return fail("unsupported RWX PT_LOAD was not rejected");
        }
    }
    {
        auto image = make_image(3, 0);
        write_u32(image, kSecondProgramHeader + 8, 0x800);
        write_u32(image, kSecondProgramHeader + 28, 1);
        MappedGuestMemory memory;
        if (load_elf32(memory, image, options).error != Elf32LoadError::SegmentPageOverlap) {
            return fail("page-overlapping PT_LOAD segments were not rejected");
        }
    }

    return 0;
}

int test_address_conflict() {
    auto image = make_image(3, 0);
    MappedGuestMemory memory;
    const std::uint32_t dynamic_base = static_cast<std::uint32_t>(memory.page_size() * 512);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(dynamic_base, memory.page_size(), rw)) {
        return fail("test setup could not pre-map target guest page");
    }

    Elf32LoadOptions options;
    options.dynamic_base = dynamic_base;
    const auto result = load_elf32(memory, image, options);
    if (result.error != Elf32LoadError::AddressConflict) {
        return fail("pre-mapped guest page was not reported as an address conflict");
    }
    if (!memory.is_mapped(dynamic_base) || memory.permissions(dynamic_base) != rw) {
        return fail("failed load modified pre-existing guest mapping");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected one test-case argument");
    }

    const std::string_view mode = argv[1];
    if (mode == "valid_dynamic") return test_valid_dynamic();
    if (mode == "relro_metadata") return test_relro_metadata();
    if (mode == "valid_exec") return test_valid_exec();
    if (mode == "dynamic_metadata") return test_dynamic_metadata();
    if (mode == "headers") return test_header_validation();
    if (mode == "ph_bounds") return test_program_header_bounds();
    if (mode == "segments") return test_segment_validation();
    if (mode == "address_conflict") return test_address_conflict();
    return fail("unknown ELF32 loader test case");
}