#include <array>
#include <cstdint>
#include <iostream>
#include <optional>

#include "memory/guest_memory.h"

namespace {

using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main() {
    MappedGuestMemory memory;
    const std::size_t page = memory.page_size();
    if (page == 0 || !memory.fastmem_base().has_value()) {
        return fail("mapped guest memory did not expose a valid reservation");
    }

    constexpr std::uint32_t base = 0x10000;
    std::array<std::uint8_t, 4> bytes{};
    if (memory.read(base, bytes)) {
        return fail("unmapped guest read unexpectedly succeeded");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(base, page, rw)) {
        return fail("page-aligned map failed");
    }
    if (!memory.is_mapped(base) || memory.permissions(base) != rw) {
        return fail("mapped page metadata mismatch");
    }
    if (memory.map(base, page, rw)) {
        return fail("overlapping map unexpectedly succeeded");
    }
    if (memory.map(base + 1, page, rw)) {
        return fail("unaligned map unexpectedly succeeded");
    }

    constexpr std::array<std::uint8_t, 4> value{0x78, 0x56, 0x34, 0x12};
    if (!memory.write(base, value) || !memory.read(base, bytes) || bytes != value) {
        return fail("mapped read/write round-trip failed");
    }

    if (!memory.protect(base, page, MemoryPermission::Read)) {
        return fail("read-only protect failed");
    }
    if (memory.write(base, value)) {
        return fail("write unexpectedly succeeded after read-only protect");
    }
    if (!memory.read(base, bytes) || bytes != value) {
        return fail("read failed after read-only protect");
    }

    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    if (!memory.protect(base, page, rx)) {
        return fail("read-execute protect failed");
    }
    if (!memory.read_code(base, bytes) || bytes != value) {
        return fail("instruction read failed on executable page");
    }

    if (memory.protect(base + static_cast<std::uint32_t>(page), page, rw)) {
        return fail("protect unexpectedly succeeded on unmapped page");
    }

    if (!memory.unmap(base, page) || memory.is_mapped(base)) {
        return fail("unmap failed");
    }
    if (memory.read(base, bytes) || memory.read_code(base, bytes) || memory.write(base, value)) {
        return fail("unmapped page remained accessible through callbacks");
    }

    if (!memory.map(base, page, rw)) {
        return fail("remap failed");
    }
    bytes.fill(0xff);
    if (!memory.read(base, bytes)) {
        return fail("read failed after remap");
    }
    if (bytes != std::array<std::uint8_t, 4>{0, 0, 0, 0}) {
        return fail("remapped anonymous page did not reset to zero");
    }

    const std::uint64_t last_page_u64 = MappedGuestMemory::kAddressSpaceSize - page;
    const auto last_page = static_cast<std::uint32_t>(last_page_u64);
    if (!memory.map(last_page, page, rw)) {
        return fail("last guest page map failed");
    }
    if (memory.map(last_page, page * 2, rw)) {
        return fail("mapping past the 32-bit guest limit unexpectedly succeeded");
    }

    if (memory.map(0x20000, page, MemoryPermission::Write)) {
        return fail("write-only fastmem mapping unexpectedly accepted");
    }
    if (memory.map(0x20000, page, MemoryPermission::Execute)) {
        return fail("execute-only fastmem mapping unexpectedly accepted");
    }

    constexpr std::uint32_t bulk_base = 0x30000;
    if (!memory.map(bulk_base, page * 4, rw)) {
        return fail("bulk-operation page range map failed");
    }
    const auto source = bulk_base + static_cast<std::uint32_t>(page) - 32U;
    const auto destination =
        bulk_base + static_cast<std::uint32_t>(page * 3) - 32U;
    std::array<std::uint8_t, 64> pattern{};
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        pattern[index] = static_cast<std::uint8_t>(index);
    }
    if (!memory.write(source, pattern) ||
        !memory.copy_bytes(destination, source, pattern.size())) {
        return fail("cross-page mapped bulk copy failed");
    }
    std::array<std::uint8_t, 64> copied{};
    if (!memory.read(destination, copied) || copied != pattern) {
        return fail("cross-page mapped bulk copy produced wrong bytes");
    }

    std::int32_t compare = 1;
    if (!memory.compare_bytes(
            source, destination, pattern.size(), compare) ||
        compare != 0) {
        return fail("cross-page mapped bulk compare failed");
    }
    if (!memory.fill_bytes(destination + 10U, 0xAA, 4U)) {
        return fail("mapped bulk fill failed");
    }
    std::optional<std::uint32_t> found;
    if (!memory.find_byte(
            destination, 0xAA, pattern.size(), found) ||
        found != std::optional<std::uint32_t>{destination + 10U}) {
        return fail("mapped bulk find returned wrong address");
    }

    if (!memory.protect(
            bulk_base + static_cast<std::uint32_t>(page * 2),
            page * 2,
            MemoryPermission::Read)) {
        return fail("bulk destination read-only protect failed");
    }
    if (memory.copy_bytes(destination, source, pattern.size()) ||
        memory.fill_bytes(destination, 0, pattern.size())) {
        return fail("mapped bulk write bypassed guest write permission");
    }

    if (!memory.unmap(
            bulk_base + static_cast<std::uint32_t>(page), page)) {
        return fail("bulk source page unmap failed");
    }
    if (memory.compare_bytes(
            source, destination, pattern.size(), compare) ||
        memory.find_byte(source, 0, pattern.size(), found)) {
        return fail("mapped bulk read ignored an unmapped page");
    }

    constexpr std::uint32_t rwx_base = 0x80000;
    const auto rwx = MemoryPermission::Read |
                     MemoryPermission::Write |
                     MemoryPermission::Execute;
    if (!memory.map(rwx_base, page, rwx)) {
        return fail("RWX bulk-generation page map failed");
    }
    const auto generation_before_bulk_write = memory.code_generation();
    if (!generation_before_bulk_write.has_value() ||
        !memory.fill_bytes(rwx_base, 0xCC, 16U) ||
        !memory.code_generation().has_value() ||
        *memory.code_generation() <= *generation_before_bulk_write ||
        !memory.direct_executable_writes_possible()) {
        return fail("bulk executable write did not advance generation");
    }

    return 0;
}
