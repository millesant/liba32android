#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace liba32android::elf::detail {

[[nodiscard]] inline std::uint16_t decode_u16_le(
    const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

[[nodiscard]] inline std::uint32_t decode_u32_le(
    const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] inline std::uint16_t decode_u16_le(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    return decode_u16_le(bytes.data() + offset);
}

[[nodiscard]] inline std::uint32_t decode_u32_le(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    return decode_u32_le(bytes.data() + offset);
}

[[nodiscard]] inline bool checked_add_guest_address(
    std::uint32_t base,
    std::uint64_t offset,
    std::uint32_t& result) noexcept {
    const std::uint64_t value = static_cast<std::uint64_t>(base) + offset;
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    result = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace liba32android::elf::detail
