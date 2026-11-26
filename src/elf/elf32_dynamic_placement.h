#pragma once

#include <cstdint>
#include <span>

#include "elf/elf32_load_error.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32DynamicPlacementOptions {
    std::uint32_t search_begin{0x10000};
    std::uint64_t search_end_exclusive{memory::MappedGuestMemory::kAddressSpaceSize};
};

enum class Elf32DynamicPlacementError : std::uint8_t {
    None = 0,
    InvalidImage,
    NotDynamic,
    InvalidSearchWindow,
    AddressOverflow,
    NoSpace,
};

struct Elf32DynamicPlacementResult {
    Elf32DynamicPlacementError error{Elf32DynamicPlacementError::None};
    Elf32LoadError load_error{Elf32LoadError::None};
    std::uint32_t dynamic_base{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32DynamicPlacementError::None;
    }
};

[[nodiscard]] Elf32DynamicPlacementResult place_elf32_dynamic(
    const memory::MappedGuestMemory& memory,
    std::span<const std::uint8_t> image,
    const Elf32DynamicPlacementOptions& options = {});

[[nodiscard]] const char* to_string(Elf32DynamicPlacementError error) noexcept;

}  // namespace liba32android::elf
