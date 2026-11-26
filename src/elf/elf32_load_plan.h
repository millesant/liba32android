#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "elf/elf32_load_error.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

enum class Elf32ImageType : std::uint8_t {
    Executable = 0,
    Dynamic,
};

struct Elf32LoadPlanSegment {
    std::uint32_t offset{};
    std::uint32_t virtual_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t alignment{};
    memory::MemoryPermission permissions{memory::MemoryPermission::None};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_end{};
};

struct Elf32LoadPlanDynamicSegment {
    std::uint32_t offset{};
    std::uint32_t virtual_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
};

struct Elf32LoadPlanRelroSegment {
    std::uint32_t virtual_address{};
    std::uint32_t memory_size{};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_end{};
};

struct Elf32LoadPlan {
    Elf32ImageType type{Elf32ImageType::Dynamic};
    std::uint32_t entry{};
    std::uint64_t minimum_page{};
    std::uint64_t maximum_page_end{};
    std::uint64_t required_load_bias_alignment{1};
    std::vector<Elf32LoadPlanSegment> segments;
    std::optional<Elf32LoadPlanDynamicSegment> dynamic_segment;
    std::vector<Elf32LoadPlanRelroSegment> relro_segments;
};

struct Elf32LoadPlanResult {
    Elf32LoadError error{Elf32LoadError::None};
    Elf32LoadPlan plan;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LoadError::None;
    }
};

[[nodiscard]] Elf32LoadPlanResult plan_elf32_load(
    const memory::MappedGuestMemory& memory,
    std::span<const std::uint8_t> image);

}  // namespace liba32android::elf
