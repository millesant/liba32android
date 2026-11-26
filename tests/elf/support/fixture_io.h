#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

#include "memory/guest_memory.h"

namespace liba32android::test_support {

[[nodiscard]] inline std::vector<std::uint8_t> read_binary_file(
    const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return {};
    }

    const std::streamoff end = input.tellg();
    if (end <= 0 ||
        static_cast<std::uint64_t>(end) >
            std::numeric_limits<std::size_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), end)) {
        return {};
    }
    return bytes;
}

[[nodiscard]] inline bool read_u32_le(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return false;
    }

    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

}  // namespace liba32android::test_support
