#include "compat/a32_libc_integer.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

struct ParseResult {
    bool success{};
    std::int32_t value{};
    std::uint32_t end_address{};
    std::int32_t errno_value{};
};

[[nodiscard]] bool is_ascii_space(std::uint8_t c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] int digit_value(std::uint8_t c) noexcept {
    if (c >= '0' && c <= '9') {
        return static_cast<int>(c - '0');
    }
    if (c >= 'a' && c <= 'z') {
        return 10 + static_cast<int>(c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return 10 + static_cast<int>(c - 'A');
    }
    return -1;
}

[[nodiscard]] bool read_at(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t offset,
    std::uint32_t max_parse_bytes,
    std::uint8_t& value) {
    if (offset >= max_parse_bytes ||
        offset > std::numeric_limits<std::uint32_t>::max() - address) {
        return false;
    }
    std::array<std::uint8_t, 1> byte{};
    if (!memory.read(address + offset, byte)) {
        return false;
    }
    value = byte[0];
    return true;
}

[[nodiscard]] bool write_u32_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t value) {
    if (address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value & 0xffU),
        static_cast<std::uint8_t>((value >> 8U) & 0xffU),
        static_cast<std::uint8_t>((value >> 16U) & 0xffU),
        static_cast<std::uint8_t>((value >> 24U) & 0xffU),
    }};
    return memory.write(address, bytes);
}

[[nodiscard]] ParseResult parse_signed_long32(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::int32_t base_argument,
    std::uint32_t max_parse_bytes) {
    ParseResult result;
    result.end_address = address;

    if (base_argument < 0 || base_argument == 1 || base_argument > 36) {
        result.success = true;
        result.errno_value = kA32AndroidEinval;
        return result;
    }

    std::uint32_t pos = 0;
    std::uint8_t c{};
    for (;;) {
        if (!read_at(memory, address, pos, max_parse_bytes, c)) {
            return result;
        }
        if (!is_ascii_space(c)) {
            break;
        }
        ++pos;
    }

    bool negative = false;
    if (c == '-' || c == '+') {
        negative = c == '-';
        ++pos;
        if (!read_at(memory, address, pos, max_parse_bytes, c)) {
            return result;
        }
    }

    std::int32_t base = base_argument;
    if ((base == 0 || base == 16) && c == '0') {
        std::uint8_t marker{};
        if (!read_at(memory, address, pos + 1U, max_parse_bytes, marker)) {
            return result;
        }
        if (marker == 'x' || marker == 'X') {
            std::uint8_t after{};
            if (!read_at(memory, address, pos + 2U, max_parse_bytes, after)) {
                return result;
            }
            const int d = digit_value(after);
            if (d >= 0 && d < 16) {
                base = 16;
                pos += 2U;
                c = after;
            }
        }
    }
    if ((base == 0 || base == 2) && c == '0') {
        std::uint8_t marker{};
        if (!read_at(memory, address, pos + 1U, max_parse_bytes, marker)) {
            return result;
        }
        if (marker == 'b' || marker == 'B') {
            std::uint8_t after{};
            if (!read_at(memory, address, pos + 2U, max_parse_bytes, after)) {
                return result;
            }
            if (after >= '0' && after <= '9') {
                base = 2;
                pos += 2U;
                c = after;
            }
        }
    }
    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    const std::uint64_t limit =
        negative ? 2147483648ULL : 2147483647ULL;
    std::uint64_t accumulator = 0;
    bool any = false;
    bool overflow = false;

    for (;;) {
        if (!read_at(memory, address, pos, max_parse_bytes, c)) {
            return result;
        }
        const int digit = digit_value(c);
        if (digit < 0 || digit >= base) {
            break;
        }

        any = true;
        if (!overflow) {
            const std::uint64_t udigit =
                static_cast<std::uint64_t>(digit);
            if (accumulator > (limit - udigit) /
                                  static_cast<std::uint64_t>(base)) {
                overflow = true;
            } else {
                accumulator =
                    accumulator * static_cast<std::uint64_t>(base) + udigit;
            }
        }
        ++pos;
    }

    result.success = true;
    result.end_address = any ? address + pos : address;
    if (overflow) {
        result.errno_value = kA32AndroidErange;
        result.value = negative
            ? std::numeric_limits<std::int32_t>::min()
            : std::numeric_limits<std::int32_t>::max();
        return result;
    }

    if (!any) {
        result.value = 0;
        return result;
    }

    const std::int64_t signed_value = negative
        ? -static_cast<std::int64_t>(accumulator)
        : static_cast<std::int64_t>(accumulator);
    result.value = static_cast<std::int32_t>(signed_value);
    return result;
}

}  // namespace

runtime::A32HostServiceDisposition A32LibcIntegerService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32LibcAtoiSvcImmediate &&
        svc_immediate != kA32LibcStrtolSvcImmediate) {
        return A32HostServiceDisposition::Unhandled;
    }

    const std::uint32_t input = regs[0];
    const bool is_strtol = svc_immediate == kA32LibcStrtolSvcImmediate;
    const std::uint32_t endptr = is_strtol ? regs[1] : 0U;
    const std::int32_t base = is_strtol
        ? std::bit_cast<std::int32_t>(regs[2])
        : 10;

    const ParseResult parsed = parse_signed_long32(
        memory, input, base, options_.max_parse_bytes);
    if (!parsed.success) {
        return A32HostServiceDisposition::Failed;
    }

    if (is_strtol && endptr != 0U &&
        !write_u32_le(memory, endptr, parsed.end_address)) {
        return A32HostServiceDisposition::Failed;
    }

    if (parsed.errno_value != 0) {
        errno_sink_.set_errno(parsed.errno_value);
    }
    regs[0] = std::bit_cast<std::uint32_t>(parsed.value);
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
