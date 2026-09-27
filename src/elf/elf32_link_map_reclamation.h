#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "elf/elf32_lifecycle.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32LinkMapReleaseOptions {
    std::uint32_t max_objects{};
    std::uint32_t max_segments{};
    std::uint64_t max_snapshot_bytes{};
};

enum class Elf32LinkMapReleaseError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidLinkMap,
    InvalidRoot,
    InvalidLiveAnchor,
    ObjectLimitExceeded,
    InvalidLifecycleState,
    SegmentLimitExceeded,
    SnapshotByteLimitExceeded,
    InvalidMapping,
    SnapshotFailed,
    UnmapFailed,
    RollbackFailed,
};

struct Elf32LinkMapReleaseResult {
    Elf32LinkMapReleaseError error{Elf32LinkMapReleaseError::None};
    std::size_t released_root{};
    std::vector<std::size_t> reclaimed_objects;
    std::size_t mappings_unmapped{};
    std::optional<std::size_t> failing_object;
    std::optional<std::size_t> failing_segment;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LinkMapReleaseError::None;
    }
};

// Release one exact persistent root and physically reclaim objects that become
// unreachable from all remaining persistent roots plus additional live anchors.
//
// This operation never executes lifecycle callbacks. Every reclaimed object must
// be either never constructed (Pending/Pending) or fully torn down
// (Complete/Complete). Stable graph slots become Retired tombstones and are not
// erased or recycled.
//
// Reclaimable load mappings are fully snapshotted before the first unmap.
// Link-map mutation is published only after all unmaps succeed. An unmap failure
// restores every touched mapping, including bytes and per-page permissions.
[[nodiscard]] Elf32LinkMapReleaseResult release_elf32_link_map_root(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    const Elf32LifecycleState& lifecycle,
    std::size_t root_object_index,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReleaseOptions& options);

[[nodiscard]] const char* to_string(
    Elf32LinkMapReleaseError error) noexcept;

}  // namespace liba32android::elf
