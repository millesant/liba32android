#include <cstdint>
#include <iostream>

#include "memory/guest_memory.h"
#include "memory/guest_va_allocator.h"

namespace {

using liba32android::memory::find_free_guest_range;
using liba32android::memory::GuestVaSearchError;
using liba32android::memory::GuestVaSearchOptions;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool expect_error(const liba32android::memory::GuestVaSearchResult& result,
                  GuestVaSearchError expected) {
    return !result && result.error == expected;
}

int test_first_fit() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    const std::uint64_t begin = page * 16;

    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin + page * 8,
        .alignment = page,
        .alignment_offset = 0,
    };
    const auto result = find_free_guest_range(memory, page * 2, options);
    if (!result || result.address != begin) {
        return fail("first free guest range was not selected");
    }
    if (memory.is_mapped(result.address)) {
        return fail("successful search unexpectedly mapped its result");
    }
    return 0;
}

int test_conflict_skip_and_non_mutation() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    const std::uint64_t begin = page * 32;
    const auto read_only = MemoryPermission::Read;

    if (!memory.map(static_cast<std::uint32_t>(begin + page), page, read_only)) {
        return fail("conflict test setup mapping failed");
    }

    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin + page * 8,
        .alignment = page,
        .alignment_offset = 0,
    };
    const auto result = find_free_guest_range(memory, page * 2, options);
    if (!result || result.address != begin + page * 2) {
        return fail("search did not skip a mapping inside earlier candidate ranges");
    }
    if (!memory.is_mapped(static_cast<std::uint32_t>(begin + page)) ||
        memory.permissions(static_cast<std::uint32_t>(begin + page)) != read_only) {
        return fail("search changed existing mapping state or permissions");
    }
    if (memory.is_mapped(result.address) ||
        memory.is_mapped(static_cast<std::uint32_t>(result.address + page))) {
        return fail("search reserved pages as a side effect");
    }
    return 0;
}

int test_alignment_offset() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    const std::uint64_t alignment = page * 4;
    const std::uint64_t begin = alignment * 4;
    const std::uint64_t offset = page * 2;

    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin + alignment * 3,
        .alignment = alignment,
        .alignment_offset = offset,
    };
    const auto result = find_free_guest_range(memory, page, options);
    if (!result || result.address != begin + offset ||
        (result.address % alignment) != offset) {
        return fail("non-zero alignment offset was not preserved");
    }
    return 0;
}

int test_16k_alignment() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    constexpr std::uint64_t alignment = 0x4000;
    if (page > alignment || (alignment % page) != 0) {
        return fail("host page size cannot exercise the required 16 KiB alignment case");
    }

    const std::uint64_t begin = page * 17;
    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin + alignment * 4,
        .alignment = alignment,
        .alignment_offset = 0,
    };
    const auto result = find_free_guest_range(memory, page, options);
    if (!result || (result.address % alignment) != 0 || result.address < begin) {
        return fail("16 KiB-equivalent alignment was not honored");
    }
    return 0;
}

int test_invalid_inputs_and_overflow() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    const std::uint64_t begin = page * 16;

    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin,
        .alignment = page,
        .alignment_offset = 0,
    };
    if (!expect_error(find_free_guest_range(memory, page, options),
                      GuestVaSearchError::InvalidRange)) {
        return fail("empty search window was not rejected");
    }

    options.end_exclusive = begin + page * 4;
    options.alignment = 0;
    if (!expect_error(find_free_guest_range(memory, page, options),
                      GuestVaSearchError::InvalidAlignment)) {
        return fail("zero alignment was not rejected");
    }

    options.alignment = page;
    options.alignment_offset = 1;
    if (!expect_error(find_free_guest_range(memory, page, options),
                      GuestVaSearchError::InvalidAlignment)) {
        return fail("non-page-compatible alignment offset was not rejected");
    }

    options.alignment_offset = 0;
    if (!expect_error(
            find_free_guest_range(memory, MappedGuestMemory::kAddressSpaceSize + page, options),
            GuestVaSearchError::SizeOverflow)) {
        return fail("oversized length was not classified as size overflow");
    }

    if (!expect_error(find_free_guest_range(memory, page + 1, options),
                      GuestVaSearchError::InvalidRange)) {
        return fail("non-page-sized length was not rejected");
    }

    options.end_exclusive = MappedGuestMemory::kAddressSpaceSize + 1;
    if (!expect_error(find_free_guest_range(memory, page, options),
                      GuestVaSearchError::InvalidRange)) {
        return fail("search window beyond the AArch32 address space was not rejected");
    }

    return 0;
}

int test_no_space() {
    MappedGuestMemory memory;
    const std::uint64_t page = memory.page_size();
    const std::uint64_t begin = page * 48;
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;

    if (!memory.map(static_cast<std::uint32_t>(begin), page * 2, rw)) {
        return fail("no-space test setup mapping failed");
    }

    GuestVaSearchOptions options{
        .begin = static_cast<std::uint32_t>(begin),
        .end_exclusive = begin + page * 2,
        .alignment = page,
        .alignment_offset = 0,
    };
    const auto result = find_free_guest_range(memory, page, options);
    if (!expect_error(result, GuestVaSearchError::NoSpace)) {
        return fail("fully occupied search window did not return no-space");
    }
    if (!memory.is_mapped(static_cast<std::uint32_t>(begin)) ||
        !memory.is_mapped(static_cast<std::uint32_t>(begin + page)) ||
        memory.permissions(static_cast<std::uint32_t>(begin)) != rw ||
        memory.permissions(static_cast<std::uint32_t>(begin + page)) != rw) {
        return fail("failed search changed existing mappings");
    }
    return 0;
}

}  // namespace

int main() {
    if (test_first_fit() != 0) return 1;
    if (test_conflict_skip_and_non_mutation() != 0) return 1;
    if (test_alignment_offset() != 0) return 1;
    if (test_16k_alignment() != 0) return 1;
    if (test_invalid_inputs_and_overflow() != 0) return 1;
    if (test_no_space() != 0) return 1;
    return 0;
}
