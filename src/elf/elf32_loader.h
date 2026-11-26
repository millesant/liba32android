#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "elf/elf32_load_types.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32LoadOptions {
    // For ET_DYN, this is the guest address where the lowest host-page-aligned
    // PT_LOAD mapping should begin. The resulting load bias must also preserve
    // every PT_LOAD p_align congruence requirement. ET_EXEC ignores this field
    // and loads at its fixed guest virtual addresses with load_bias == 0.
    std::optional<std::uint32_t> dynamic_base;
};

[[nodiscard]] Elf32LoadResult load_elf32(memory::MappedGuestMemory& memory,
                                         std::span<const std::uint8_t> image,
                                         const Elf32LoadOptions& options = {});

}  // namespace liba32android::elf
