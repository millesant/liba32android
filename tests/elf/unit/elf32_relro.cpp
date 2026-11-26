#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

#include "elf/elf32_relro.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32LoadResult;
using liba32android::elf::Elf32RelroError;
using liba32android::elf::Elf32RelroOptions;
using liba32android::elf::Elf32RelroSegment;
using liba32android::elf::seal_elf32_gnu_relro;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::uint32_t test_base(const MappedGuestMemory& memory) {
    return static_cast<std::uint32_t>(memory.page_size() * 16U);
}

Elf32RelroSegment one_page_relro(
    const MappedGuestMemory& memory,
    std::uint32_t page) {
    return Elf32RelroSegment{
        .guest_address = page + 0x20,
        .memory_size = 0x40,
        .mapping_start = page,
        .mapping_size = memory.page_size(),
    };
}

int test_empty_success() {
    MappedGuestMemory memory;
    Elf32LoadResult load;
    const auto result = seal_elf32_gnu_relro(memory, load, {});
    if (!result || result.sealed_pages != 0) {
        return fail("empty RELRO metadata did not succeed as empty work");
    }
    return 0;
}

int test_seal_and_idempotence() {
    MappedGuestMemory memory;
    const std::size_t page_size = memory.page_size();
    const std::uint32_t base = test_base(memory);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;

    if (!memory.map(base, page_size * 2U, rw)) {
        return fail("could not map synthetic RELRO pages");
    }

    constexpr std::array<std::uint8_t, 4> marker{0x11, 0x22, 0x33, 0x44};
    if (!memory.write(base + 0x20, marker) ||
        !memory.write(base + static_cast<std::uint32_t>(page_size) + 0x20, marker)) {
        return fail("could not seed synthetic RELRO bytes");
    }

    Elf32LoadResult load;
    load.relro_segments.push_back({
        .guest_address = base + 0x20,
        .memory_size = static_cast<std::uint32_t>(page_size + 0x40),
        .mapping_start = base,
        .mapping_size = page_size * 2U,
    });

    const auto sealed = seal_elf32_gnu_relro(
        memory, load, Elf32RelroOptions{.max_pages = 2});
    if (!sealed || sealed.sealed_pages != 2 ||
        memory.permissions(base) != MemoryPermission::Read ||
        memory.permissions(base + static_cast<std::uint32_t>(page_size)) !=
            MemoryPermission::Read) {
        return fail("synthetic RELRO pages were not sealed read-only");
    }

    std::array<std::uint8_t, 4> actual{};
    if (!memory.read(base + 0x20, actual) || actual != marker) {
        return fail("RELRO sealing changed guest bytes");
    }
    constexpr std::array<std::uint8_t, 1> byte{0xaa};
    if (memory.write(base + 0x20, byte)) {
        return fail("write succeeded after RELRO sealing");
    }

    const auto repeated = seal_elf32_gnu_relro(
        memory, load, Elf32RelroOptions{.max_pages = 2});
    if (!repeated || repeated.sealed_pages != 2 ||
        memory.permissions(base) != MemoryPermission::Read) {
        return fail("RELRO sealing was not idempotent");
    }
    return 0;
}

int test_overlap_and_page_bound() {
    MappedGuestMemory memory;
    const std::size_t page_size = memory.page_size();
    const std::uint32_t base = test_base(memory);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(base, page_size, rw)) {
        return fail("could not map overlap RELRO page");
    }

    Elf32LoadResult load;
    load.relro_segments.push_back(one_page_relro(memory, base));
    load.relro_segments.push_back(one_page_relro(memory, base));

    const auto bounded = seal_elf32_gnu_relro(
        memory, load, Elf32RelroOptions{.max_pages = 1});
    if (bounded || bounded.error != Elf32RelroError::PageLimitExceeded ||
        memory.permissions(base) != rw) {
        return fail("declared RELRO page occurrences did not honor the caller bound pre-write");
    }

    const auto sealed = seal_elf32_gnu_relro(
        memory, load, Elf32RelroOptions{.max_pages = 2});
    if (!sealed || sealed.sealed_pages != 1 ||
        memory.permissions(base) != MemoryPermission::Read) {
        return fail("overlapping RELRO descriptors were not deduplicated for sealing");
    }
    return 0;
}

int test_invalid_metadata_and_options() {
    {
        MappedGuestMemory memory;
        Elf32LoadResult load;
        const std::uint32_t base = test_base(memory);
        load.relro_segments.push_back(one_page_relro(memory, base));
        const auto result = seal_elf32_gnu_relro(memory, load, {});
        if (result || result.error != Elf32RelroError::InvalidOptions) {
            return fail("present RELRO metadata accepted zero max_pages");
        }
    }

    {
        MappedGuestMemory memory;
        Elf32LoadResult load;
        const std::uint32_t base = test_base(memory);
        auto relro = one_page_relro(memory, base);
        ++relro.mapping_start;
        load.relro_segments.push_back(relro);
        const auto result = seal_elf32_gnu_relro(
            memory, load, Elf32RelroOptions{.max_pages = 1});
        if (result || result.error != Elf32RelroError::InvalidMetadata) {
            return fail("unaligned RELRO mapping metadata was not rejected");
        }
    }

    {
        MappedGuestMemory memory;
        Elf32LoadResult load;
        load.relro_segments.push_back({
            .guest_address = std::numeric_limits<std::uint32_t>::max() - 0x10U,
            .memory_size = 0x40,
            .mapping_start = 0,
            .mapping_size = memory.page_size(),
        });
        const auto result = seal_elf32_gnu_relro(
            memory, load, Elf32RelroOptions{.max_pages = 1});
        if (result || result.error != Elf32RelroError::InvalidMetadata) {
            return fail("overflowing exact RELRO metadata was not rejected");
        }
    }

    return 0;
}

int test_preflight_failures_do_not_mutate() {
    {
        MappedGuestMemory memory;
        const std::size_t page_size = memory.page_size();
        const std::uint32_t base = test_base(memory);
        const auto rw = MemoryPermission::Read | MemoryPermission::Write;
        if (!memory.map(base, page_size, rw)) {
            return fail("could not map preflight first page");
        }

        Elf32LoadResult load;
        load.relro_segments.push_back(one_page_relro(memory, base));
        load.relro_segments.push_back(one_page_relro(
            memory, base + static_cast<std::uint32_t>(page_size)));

        const auto result = seal_elf32_gnu_relro(
            memory, load, Elf32RelroOptions{.max_pages = 2});
        if (result || result.error != Elf32RelroError::UnmappedPage ||
            memory.permissions(base) != rw) {
            return fail("unmapped-page preflight mutated an earlier RELRO page");
        }
    }

    {
        MappedGuestMemory memory;
        const std::size_t page_size = memory.page_size();
        const std::uint32_t base = test_base(memory);
        if (!memory.map(base, page_size, MemoryPermission::None)) {
            return fail("could not map unreadable RELRO page");
        }

        Elf32LoadResult load;
        load.relro_segments.push_back(one_page_relro(memory, base));
        const auto result = seal_elf32_gnu_relro(
            memory, load, Elf32RelroOptions{.max_pages = 1});
        if (result || result.error != Elf32RelroError::UnreadablePage ||
            memory.permissions(base) != MemoryPermission::None) {
            return fail("unreadable RELRO page was not rejected without mutation");
        }
    }

    {
        MappedGuestMemory memory;
        const std::size_t page_size = memory.page_size();
        const std::uint32_t base = test_base(memory);
        const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
        if (!memory.map(base, page_size, rx)) {
            return fail("could not map executable RELRO page");
        }

        Elf32LoadResult load;
        load.relro_segments.push_back(one_page_relro(memory, base));
        const auto result = seal_elf32_gnu_relro(
            memory, load, Elf32RelroOptions{.max_pages = 1});
        if (result || result.error != Elf32RelroError::ExecutablePage ||
            memory.permissions(base) != rx) {
            return fail("executable RELRO page was not rejected without mutation");
        }
    }

    return 0;
}

}  // namespace

int main() {
    if (test_empty_success() != 0) return 1;
    if (test_seal_and_idempotence() != 0) return 1;
    if (test_overlap_and_page_bound() != 0) return 1;
    if (test_invalid_metadata_and_options() != 0) return 1;
    if (test_preflight_failures_do_not_mutate() != 0) return 1;
    return 0;
}
