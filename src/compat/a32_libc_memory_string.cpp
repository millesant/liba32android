#include "compat/a32_libc_memory_string.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

constexpr std::size_t kStringScanChunkBytes = 256U;

struct ProgressiveReadResult {
    std::size_t bytes_read{};
    bool complete{};
};

[[nodiscard]] std::size_t next_string_chunk_size(
    std::uint32_t address,
    std::uint64_t offset,
    std::uint64_t remaining) noexcept {
    if (remaining == 0) return 0;
    const std::uint64_t current =
        static_cast<std::uint64_t>(address) + offset;
    if (current > std::numeric_limits<std::uint32_t>::max()) return 0;
    const std::uint64_t until_wrap =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
        current + 1U;
    return static_cast<std::size_t>(std::min({
        remaining,
        until_wrap,
        static_cast<std::uint64_t>(kStringScanChunkBytes),
    }));
}

[[nodiscard]] ProgressiveReadResult read_progressive_chunk(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::span<std::uint8_t> output) {
    if (output.empty()) return {0, true};
    if (memory.read(address, output)) {
        return {output.size(), true};
    }

    for (std::size_t index = 0; index < output.size(); ++index) {
        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(
                address + static_cast<std::uint32_t>(index), byte)) {
            return {index, false};
        }
        output[index] = byte[0];
    }
    return {output.size(), true};
}

[[nodiscard]] bool read_c_string_with_nul(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t max_payload_bytes,
    std::vector<std::uint8_t>& output) {
    output.clear();
    std::array<std::uint8_t, kStringScanChunkBytes> buffer{};
    std::uint64_t offset = 0;
    std::uint64_t remaining =
        static_cast<std::uint64_t>(max_payload_bytes) + 1U;

    while (remaining != 0) {
        const std::size_t chunk_size =
            next_string_chunk_size(address, offset, remaining);
        if (chunk_size == 0) return false;
        const auto current_address = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(address) + offset);
        const ProgressiveReadResult read = read_progressive_chunk(
            memory,
            current_address,
            std::span<std::uint8_t>{buffer}.first(chunk_size));
        for (std::size_t index = 0; index < read.bytes_read; ++index) {
            output.push_back(buffer[index]);
            if (buffer[index] == 0) return true;
        }
        if (!read.complete) return false;
        offset += chunk_size;
        remaining -= chunk_size;
    }
    return false;
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

[[nodiscard]] bool find_c_string_length(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t max_payload_bytes,
    std::uint32_t& length) {
    std::array<std::uint8_t, kStringScanChunkBytes> buffer{};
    std::uint64_t offset = 0;
    std::uint64_t remaining =
        static_cast<std::uint64_t>(max_payload_bytes) + 1U;

    while (remaining != 0) {
        const std::size_t chunk_size =
            next_string_chunk_size(address, offset, remaining);
        if (chunk_size == 0) return false;
        const auto current_address = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(address) + offset);
        const ProgressiveReadResult read = read_progressive_chunk(
            memory,
            current_address,
            std::span<std::uint8_t>{buffer}.first(chunk_size));
        for (std::size_t index = 0; index < read.bytes_read; ++index) {
            if (buffer[index] == 0) {
                length = static_cast<std::uint32_t>(
                    offset + static_cast<std::uint64_t>(index));
                return true;
            }
        }
        if (!read.complete) return false;
        offset += chunk_size;
        remaining -= chunk_size;
    }
    return false;
}

[[nodiscard]] bool compare_c_strings(
    const memory::GuestMemory& memory,
    std::uint32_t lhs_address,
    std::uint32_t rhs_address,
    std::uint64_t max_bytes,
    std::int32_t& result) {
    result = 0;
    std::array<std::uint8_t, kStringScanChunkBytes> lhs_buffer{};
    std::array<std::uint8_t, kStringScanChunkBytes> rhs_buffer{};
    std::uint64_t offset = 0;
    std::uint64_t remaining = max_bytes;

    while (remaining != 0) {
        const std::size_t lhs_chunk =
            next_string_chunk_size(lhs_address, offset, remaining);
        const std::size_t rhs_chunk =
            next_string_chunk_size(rhs_address, offset, remaining);
        const std::size_t chunk_size = std::min(lhs_chunk, rhs_chunk);
        if (chunk_size == 0) return false;

        const auto lhs_current = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(lhs_address) + offset);
        const auto rhs_current = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(rhs_address) + offset);
        const ProgressiveReadResult lhs_read = read_progressive_chunk(
            memory,
            lhs_current,
            std::span<std::uint8_t>{lhs_buffer}.first(chunk_size));
        const ProgressiveReadResult rhs_read = read_progressive_chunk(
            memory,
            rhs_current,
            std::span<std::uint8_t>{rhs_buffer}.first(chunk_size));

        const std::size_t comparable =
            std::min(lhs_read.bytes_read, rhs_read.bytes_read);
        for (std::size_t index = 0; index < comparable; ++index) {
            result = byte_compare(lhs_buffer[index], rhs_buffer[index]);
            if (result != 0) return true;
            if (lhs_buffer[index] == 0) {
                result = 0;
                return true;
            }
        }
        if (!lhs_read.complete || !rhs_read.complete ||
            lhs_read.bytes_read != chunk_size ||
            rhs_read.bytes_read != chunk_size) {
            return false;
        }
        offset += chunk_size;
        remaining -= chunk_size;
    }
    result = 0;
    return true;
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

        if (!memory.copy_bytes(destination, source, count)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = destination;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcMemmoveSvcImmediate: {
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

        if (!memory.copy_bytes(destination, source, count)) {
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

        if (!memory.fill_bytes(destination, value, count)) {
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

        std::int32_t result = 0;
        if (!memory.compare_bytes(
                lhs_address, rhs_address, count, result)) {
            return A32HostServiceDisposition::Failed;
        }
        write_signed_result(regs, result);
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

        std::optional<std::uint32_t> found_address;
        if (!memory.find_byte(address, value, count, found_address)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = found_address.value_or(0U);
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrlenSvcImmediate: {
        const std::uint32_t address = regs[0];
        std::uint32_t length = 0;
        if (!find_c_string_length(
                memory, address, options_.max_string_bytes, length)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = length;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrcmpSvcImmediate: {
        const std::uint32_t lhs_address = regs[0];
        const std::uint32_t rhs_address = regs[1];
        std::int32_t result = 0;
        if (!compare_c_strings(
                memory,
                lhs_address,
                rhs_address,
                static_cast<std::uint64_t>(options_.max_string_bytes) + 1U,
                result)) {
            return A32HostServiceDisposition::Failed;
        }
        write_signed_result(regs, result);
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrncmpSvcImmediate: {
        const std::uint32_t lhs_address = regs[0];
        const std::uint32_t rhs_address = regs[1];
        const std::uint32_t count = regs[2];
        if (count > options_.max_string_bytes) {
            return A32HostServiceDisposition::Failed;
        }
        std::int32_t result = 0;
        if (!compare_c_strings(
                memory, lhs_address, rhs_address, count, result)) {
            return A32HostServiceDisposition::Failed;
        }
        write_signed_result(regs, result);
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcMemmemSvcImmediate: {
        const std::uint32_t haystack_address = regs[0];
        const std::uint32_t haystack_size = regs[1];
        const std::uint32_t needle_address = regs[2];
        const std::uint32_t needle_size = regs[3];
        if (haystack_size > options_.max_transfer_bytes ||
            needle_size > options_.max_transfer_bytes) {
            return A32HostServiceDisposition::Failed;
        }
        // Android 17 uses the OpenBSD/musl memmem shape: an empty needle
        // returns haystack immediately, and a shorter haystack returns null,
        // before either byte range is dereferenced.
        if (needle_size == 0) {
            regs[0] = haystack_address;
            return A32HostServiceDisposition::Handled;
        }
        if (haystack_size < needle_size) {
            regs[0] = 0;
            return A32HostServiceDisposition::Handled;
        }
        if (!range_fits_u32(haystack_address, haystack_size) ||
            !range_fits_u32(needle_address, needle_size)) {
            return A32HostServiceDisposition::Failed;
        }

        std::vector<std::uint8_t> haystack(haystack_size);
        std::vector<std::uint8_t> needle(needle_size);
        if (!memory.read(haystack_address, haystack) ||
            !memory.read(needle_address, needle)) {
            return A32HostServiceDisposition::Failed;
        }

        const auto found = std::search(
            haystack.begin(), haystack.end(),
            needle.begin(), needle.end());
        if (found == haystack.end()) {
            regs[0] = 0;
            return A32HostServiceDisposition::Handled;
        }

        regs[0] = haystack_address +
            static_cast<std::uint32_t>(
                std::distance(haystack.begin(), found));
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrcpySvcImmediate: {
        const std::uint32_t destination = regs[0];
        const std::uint32_t source = regs[1];

        std::vector<std::uint8_t> bytes;
        if (!read_c_string_with_nul(
                memory, source, options_.max_string_bytes, bytes) ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return A32HostServiceDisposition::Failed;
        }
        const auto size = static_cast<std::uint32_t>(bytes.size());
        if (size > options_.max_transfer_bytes ||
            !range_fits_u32(destination, size)) {
            return A32HostServiceDisposition::Failed;
        }
        if (!memory.write(destination, bytes)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = destination;
        return A32HostServiceDisposition::Handled;
    }

    case kA32LibcStrncpySvcImmediate: {
        const std::uint32_t destination = regs[0];
        const std::uint32_t source = regs[1];
        const std::uint32_t count = regs[2];
        if (count > options_.max_transfer_bytes ||
            !range_fits_u32(destination, count)) {
            return A32HostServiceDisposition::Failed;
        }
        if (count == 0) {
            return A32HostServiceDisposition::Handled;
        }

        // Only source bytes actually consumed before NUL need to be
        // addressable. Remaining output bytes are destination padding.
        std::vector<std::uint8_t> bytes(count, 0);
        std::array<std::uint8_t, kStringScanChunkBytes> buffer{};
        std::uint64_t offset = 0;
        std::uint64_t remaining = count;
        while (remaining != 0) {
            const std::size_t chunk_size =
                next_string_chunk_size(source, offset, remaining);
            if (chunk_size == 0) {
                return A32HostServiceDisposition::Failed;
            }
            const auto current_address = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(source) + offset);
            const ProgressiveReadResult read = read_progressive_chunk(
                memory,
                current_address,
                std::span<std::uint8_t>{buffer}.first(chunk_size));
            bool terminated = false;
            for (std::size_t index = 0; index < read.bytes_read; ++index) {
                bytes[static_cast<std::size_t>(offset) + index] = buffer[index];
                if (buffer[index] == 0) {
                    terminated = true;
                    break;
                }
            }
            if (terminated) break;
            if (!read.complete) {
                return A32HostServiceDisposition::Failed;
            }
            offset += chunk_size;
            remaining -= chunk_size;
        }
        if (!memory.write(destination, bytes)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = destination;
        return A32HostServiceDisposition::Handled;
    }

    default:
        return A32HostServiceDisposition::Unhandled;
    }
}

}  // namespace liba32android::compat
