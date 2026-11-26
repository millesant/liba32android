#pragma once

#include <cstdint>

#include "memory/guest_memory.h"

namespace liba32android::memory {

struct GuestVaSearchOptions {
    std::uint32_t begin{};
    std::uint64_t end_exclusive{};
    std::uint64_t alignment{};
    std::uint64_t alignment_offset{};
};

enum class GuestVaSearchError : std::uint8_t {
    None = 0,
    InvalidRange,
    InvalidAlignment,
    SizeOverflow,
    NoSpace,
};

struct GuestVaSearchResult {
    GuestVaSearchError error{GuestVaSearchError::None};
    std::uint32_t address{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == GuestVaSearchError::None;
    }
};

[[nodiscard]] GuestVaSearchResult find_free_guest_range(
    const MappedGuestMemory& memory,
    std::uint64_t length,
    const GuestVaSearchOptions& options);

[[nodiscard]] const char* to_string(GuestVaSearchError error) noexcept;

}  // namespace liba32android::memory
