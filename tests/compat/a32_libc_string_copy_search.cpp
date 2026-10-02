#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

#include "compat/a32_libc_memory_string.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::compat::A32LibcMemoryStringOptions;
using liba32android::compat::A32LibcMemoryStringService;
using liba32android::compat::kA32LibcMemmemSvcImmediate;
using liba32android::compat::kA32LibcStrcpySvcImmediate;
using liba32android::compat::kA32LibcStrncpySvcImmediate;
using liba32android::memory::LinearGuestMemory;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceDisposition;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

int test_memmem() {
    LinearGuestMemory memory{512};
    constexpr std::array<std::uint8_t, 8> haystack{
        'a','b','c','d','a','b','c','d',
    };
    constexpr std::array<std::uint8_t, 3> needle{
        'b','c','d',
    };
    if (!memory.write(0x100U, haystack) ||
        !memory.write(0x140U, needle)) {
        return fail("could not stage memmem inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{16, 16}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x100U;
    regs[1] = haystack.size();
    regs[2] = 0x140U;
    regs[3] = needle.size();
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x101U) {
        return fail("memmem did not return first logical guest match");
    }

    regs = {};
    regs[0] = 0x1ffU;
    regs[1] = 0U;
    regs[2] = 0xffffffffU;
    regs[3] = 0U;
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x1ffU) {
        return fail("empty memmem needle did not avoid guest reads");
    }

    regs = {};
    regs[0] = 0xfffffffcU;
    regs[1] = 8U;
    regs[2] = 0xffffffffU;
    regs[3] = 0U;
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xfffffffcU) {
        return fail("empty memmem needle validated unused haystack range");
    }

    regs = {};
    regs[0] = 0x100U;
    regs[1] = 2U;
    regs[2] = 0xffffffffU;
    regs[3] = 3U;
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("short haystack memmem did not return null before reads");
    }

    regs = {};
    regs[0] = 0xfffffffcU;
    regs[1] = 8U;
    regs[2] = 0x140U;
    regs[3] = 3U;
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("memmem logical range wrap did not fail");
    }

    constexpr std::array<std::uint8_t, 4> partial_match{
        'b','c','d','x',
    };
    if (!memory.write(0x1fcU, partial_match)) {
        return fail("could not stage partial unreadable memmem range");
    }
    regs = {};
    regs[0] = 0x1fcU;
    regs[1] = 8U;
    regs[2] = 0x140U;
    regs[3] = 3U;
    if (service.handle(memory, kA32LibcMemmemSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("memmem stopped validating after an early match");
    }
    return 0;
}

int test_strcpy() {
    LinearGuestMemory memory{512};
    constexpr std::array<std::uint8_t, 6> source{
        'h','e','l','l','o',0,
    };
    constexpr std::array<std::uint8_t, 6> original{
        9,9,9,9,9,9,
    };
    if (!memory.write(0x100U, source) ||
        !memory.write(0x140U, original)) {
        return fail("could not stage strcpy inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{8, 5}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x140U;
    regs[1] = 0x100U;
    if (service.handle(memory, kA32LibcStrcpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x140U) {
        return fail("strcpy did not return destination");
    }
    std::array<std::uint8_t, 6> copied{};
    if (!memory.read(0x140U, copied) || copied != source) {
        return fail("strcpy did not copy terminating NUL");
    }

    A32LibcMemoryStringService tight_service{
        A32LibcMemoryStringOptions{8, 4}};
    if (!memory.write(0x140U, original)) {
        return fail("could not reset strcpy destination");
    }
    regs = {};
    regs[0] = 0x140U;
    regs[1] = 0x100U;
    if (tight_service.handle(
            memory, kA32LibcStrcpySvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("strcpy string ceiling was not enforced");
    }
    if (!memory.read(0x140U, copied) || copied != original) {
        return fail("strcpy source failure mutated destination");
    }
    return 0;
}

int test_strncpy() {
    LinearGuestMemory memory{512};
    constexpr std::array<std::uint8_t, 3> short_source{
        'h','i',0,
    };
    constexpr std::array<std::uint8_t, 6> long_source{
        'a','b','c','d','e','f',
    };
    if (!memory.write(0x100U, short_source) ||
        !memory.write(0x120U, long_source)) {
        return fail("could not stage strncpy inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{8, 8}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x160U;
    regs[1] = 0x100U;
    regs[2] = 5U;
    if (service.handle(memory, kA32LibcStrncpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x160U) {
        return fail("strncpy padded-copy call failed");
    }
    std::array<std::uint8_t, 5> padded{};
    if (!memory.read(0x160U, padded) ||
        padded != std::array<std::uint8_t, 5>{{'h','i',0,0,0}}) {
        return fail("strncpy did not pad destination with NUL bytes");
    }

    regs = {};
    regs[0] = 0x180U;
    regs[1] = 0x120U;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcStrncpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0x180U) {
        return fail("strncpy truncating call failed");
    }
    std::array<std::uint8_t, 4> truncated{};
    if (!memory.read(0x180U, truncated) ||
        truncated != std::array<std::uint8_t, 4>{{'a','b','c','d'}}) {
        return fail("strncpy did not preserve non-terminated truncation semantics");
    }

    regs = {};
    regs[0] = 0xffffffffU;
    regs[1] = 0xffffffffU;
    regs[2] = 0U;
    if (service.handle(memory, kA32LibcStrncpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU) {
        return fail("zero-count strncpy accessed guest memory");
    }

    constexpr std::array<std::uint8_t, 4> destination_sentinel{9,9,9,9};
    constexpr std::array<std::uint8_t, 2> unterminated_tail{'q','r'};
    if (!memory.write(0x1a0U, destination_sentinel) ||
        !memory.write(0x1feU, unterminated_tail)) {
        return fail("could not stage strncpy source-fault fixture");
    }
    regs = {};
    regs[0] = 0x1a0U;
    regs[1] = 0x1feU;
    regs[2] = 4U;
    if (service.handle(memory, kA32LibcStrncpySvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("strncpy unreadable source did not fail");
    }
    std::array<std::uint8_t, 4> destination_after{};
    if (!memory.read(0x1a0U, destination_after) ||
        destination_after != destination_sentinel) {
        return fail("strncpy source failure mutated destination");
    }
    return 0;
}

int test_strncpy_stops_reading_after_nul() {
    MappedGuestMemory memory;
    const std::uint64_t page_size = memory.page_size();
    if (page_size == 0 ||
        page_size > std::numeric_limits<std::uint32_t>::max() / 3U) {
        return fail("invalid mapped-memory page size");
    }

    const std::uint32_t destination_page =
        static_cast<std::uint32_t>(page_size * 2U);
    const std::uint32_t final_page =
        static_cast<std::uint32_t>(
            MappedGuestMemory::kAddressSpaceSize - page_size);
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(destination_page, page_size, rw) ||
        !memory.map(final_page, page_size, rw)) {
        return fail("could not map high-address strncpy regression pages");
    }

    constexpr std::array<std::uint8_t, 2> source{{'A', 0}};
    constexpr std::uint32_t source_address = 0xfffffffeU;
    if (!memory.write(source_address, source)) {
        return fail("could not stage high-address strncpy source");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{8, 8}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = destination_page;
    regs[1] = source_address;
    regs[2] = 4U;

    if (service.handle(memory, kA32LibcStrncpySvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != destination_page) {
        return fail("strncpy rejected unread bytes after an observed NUL");
    }

    std::array<std::uint8_t, 4> copied{};
    if (!memory.read(destination_page, copied) ||
        copied != std::array<std::uint8_t, 4>{{'A', 0, 0, 0}}) {
        return fail("high-address strncpy did not preserve NUL padding");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_memmem(); status != 0) {
        return status;
    }
    if (const int status = test_strcpy(); status != 0) {
        return status;
    }
    if (const int status = test_strncpy(); status != 0) {
        return status;
    }
    if (const int status = test_strncpy_stops_reading_after_nul(); status != 0) {
        return status;
    }
    return 0;
}
