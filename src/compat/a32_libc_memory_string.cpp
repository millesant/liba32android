#include "compat/a32_libc_memory_string.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

[[nodiscard]] bool range_fits_u32(
    std::uint32_t address,
    std::uint32_t size) noexcept {
    if (size == 0) {
        return true;
    }
    return static_cast<std::uint64_t>(address) +
               static_cast<std::uint64_t>(size) - 1U <=
           std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool read_byte(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t offset,
    std::uint8_t& value) {
    if (offset > std::numeric_limits<std::uint32_t>::max() - address) {
        return false;
    }
    std::array<std::uint8_t, 1> byte{};
    if (!memory.read(address + offset, byte)) {
        return false;
    }
    value = byte[0];
    return true;
}

void write_signed_result(
    std::array<std::uint32_t, 16>& regs,
    std::int32_t value) noexcept {
    regs[0] = std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t byte_compare(
    std::uint8_t lhs,
    std::uint8_t rhs) noexcept {
    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    return 0;
}

}  // namespace

runtime::A32HostServiceDisposition A32LibcMemoryStringService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    switch (svc_immediate) {
    case kA32LibcMemcpySvcImmediate: {
        const std::uint32_t destination = regs[0];
        const std::uint32_t source = regs[1];
        const std::uint32_t count = regs[2];
        if (count > options_.max_transfer_bytes ||
            !range_fits_u32(destination, count) ||
            !range_fits_u32(source, count)) {
            return A32HostServiceDisposition::Failed;
        }
        if (count == 0) {
            return A32HostServiceDisposition::Handled;
        }

        std::vector<std::uint8_t> bytes(count);
        if (!memory.read(source, bytes) ||
            !memory.write(destination, bytes)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = destination;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcMemsetSvcImmediate: {
        const std::uint32_t destination = regs[0];
        const std::uint8_t value =
            static_cast<std::uint8_t>(regs[1] & 0xffU);
        const std::uint32_t count = regs[2];
        if (count > options_.max_transfer_bytes ||
            !range_fits_u32(destination, count)) {
            return A32HostServiceDisposition::Failed;
        }
        if (count == 0) {
            return A32HostServiceDisposition::Handled;
        }

        const std::vector<std::uint8_t> bytes(count, value);
        if (!memory.write(destination, bytes)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = destination;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcMemcmpSvcImmediate: {
        const std::uint32_t lhs_address = regs[0];
        const std::uint32_t rhs_address = regs[1];
        const std::uint32_t count = regs[2];
        if (count > options_.max_transfer_bytes ||
            !range_fits_u32(lhs_address, count) ||
            !range_fits_u32(rhs_address, count)) {
            return A32HostServiceDisposition::Failed;
        }
        if (count == 0) {
            write_signed_result(regs, 0);
            return A32HostServiceDisposition::Handled;
        }

        std::vector<std::uint8_t> lhs(count);
        std::vector<std::uint8_t> rhs(count);
        if (!memory.read(lhs_address, lhs) ||
            !memory.read(rhs_address, rhs)) {
            return A32HostServiceDisposition::Failed;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::int32_t result = byte_compare(lhs[i], rhs[i]);
            if (result != 0) {
                write_signed_result(regs, result);
                return A32HostServiceDisposition::Handled;
            }
        }
        write_signed_result(regs, 0);
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcMemchrSvcImmediate: {
        const std::uint32_t address = regs[0];
        const std::uint8_t value =
            static_cast<std::uint8_t>(regs[1] & 0xffU);
        const std::uint32_t count = regs[2];
        if (count > options_.max_transfer_bytes ||
            !range_fits_u32(address, count)) {
            return A32HostServiceDisposition::Failed;
        }
        if (count == 0) {
            regs[0] = 0;
            return A32HostServiceDisposition::Handled;
        }

        std::vector<std::uint8_t> bytes(count);
        if (!memory.read(address, bytes)) {
            return A32HostServiceDisposition::Failed;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (bytes[i] == value) {
                regs[0] = address + i;
                return A32HostServiceDisposition::Handled;
            }
        }
        regs[0] = 0;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrlenSvcImmediate: {
        const std::uint32_t address = regs[0];
        for (std::uint32_t offset = 0;; ++offset) {
            std::uint8_t byte{};
            if (!read_byte(memory, address, offset, byte)) {
                return A32HostServiceDisposition::Failed;
            }
            if (byte == 0) {
                regs[0] = offset;
                return A32HostServiceDisposition::Handled;
            }
            if (offset == options_.max_string_bytes) {
                return A32HostServiceDisposition::Failed;
            }
        }
    }

    case kA32LibcStrcmpSvcImmediate: {
        const std::uint32_t lhs_address = regs[0];
        const std::uint32_t rhs_address = regs[1];
        for (std::uint32_t offset = 0;; ++offset) {
            std::uint8_t lhs{};
            std::uint8_t rhs{};
            if (!read_byte(memory, lhs_address, offset, lhs) ||
                !read_byte(memory, rhs_address, offset, rhs)) {
                return A32HostServiceDisposition::Failed;
            }
            const std::int32_t result = byte_compare(lhs, rhs);
            if (result != 0) {
                write_signed_result(regs, result);
                return A32HostServiceDisposition::Handled;
            }
            if (lhs == 0) {
                write_signed_result(regs, 0);
                return A32HostServiceDisposition::Handled;
            }
            if (offset == options_.max_string_bytes) {
                return A32HostServiceDisposition::Failed;
            }
        }
    }

    case kA32LibcStrncmpSvcImmediate: {
        const std::uint32_t lhs_address = regs[0];
        const std::uint32_t rhs_address = regs[1];
        const std::uint32_t count = regs[2];
        if (count > options_.max_string_bytes) {
            return A32HostServiceDisposition::Failed;
        }
        for (std::uint32_t offset = 0; offset < count; ++offset) {
            std::uint8_t lhs{};
            std::uint8_t rhs{};
            if (!read_byte(memory, lhs_address, offset, lhs) ||
                !read_byte(memory, rhs_address, offset, rhs)) {
                return A32HostServiceDisposition::Failed;
            }
            const std::int32_t result = byte_compare(lhs, rhs);
            if (result != 0) {
                write_signed_result(regs, result);
                return A32HostServiceDisposition::Handled;
            }
            if (lhs == 0) {
                write_signed_result(regs, 0);
                return A32HostServiceDisposition::Handled;
            }
        }
        write_signed_result(regs, 0);
        return A32HostServiceDisposition::Handled;
    }

    default:
        return A32HostServiceDisposition::Unhandled;
    }
}

}  // namespace liba32android::compat
