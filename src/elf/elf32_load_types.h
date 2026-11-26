#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "elf/elf32_load_error.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32LoadedSegment {
    std::uint32_t guest_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_size{};
    memory::MemoryPermission permissions{memory::MemoryPermission::None};
};

struct Elf32DynamicSegment {
    // Guest VA after applying load_bias. No host pointer crosses this boundary.
    std::uint32_t guest_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
};

struct Elf32RelroSegment {
    // Exact PT_GNU_RELRO guest range after applying load_bias.
    std::uint32_t guest_address{};
    std::uint32_t memory_size{};
    // Host-page-rounded guest range that a later RELRO sealing layer may
    // protect. The loader itself does not change permissions for this range.
    std::uint32_t mapping_start{};
    std::uint64_t mapping_size{};
};

struct Elf32LoadResult {
    Elf32LoadError error{Elf32LoadError::None};
    std::uint32_t load_bias{};
    std::uint32_t entry{};
    std::vector<Elf32LoadedSegment> segments;
    // Missing PT_DYNAMIC is valid and leaves this empty. A present PT_DYNAMIC
    // is validated as one unique, non-empty range inside a readable PT_LOAD,
    // with its file bytes matching that PT_LOAD's file-to-guest mapping.
    std::optional<Elf32DynamicSegment> dynamic_segment;
    // Zero or more validated PT_GNU_RELRO ranges in program-header order.
    // These are metadata only; load_elf32 leaves PT_LOAD permissions intact so
    // relocations can complete before a downstream explicit sealing call.
    std::vector<Elf32RelroSegment> relro_segments;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LoadError::None;
    }
};

}  // namespace liba32android::elf
