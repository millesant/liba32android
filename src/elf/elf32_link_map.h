#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "elf/elf32_dependency_graph.h"

namespace liba32android::elf {

enum class Elf32LinkMapRootPolicy : std::uint8_t {
    Local = 0,
    Global,
};

struct Elf32LinkMapRoot {
    std::size_t object_index{};
    Elf32LinkMapRootPolicy policy{Elf32LinkMapRootPolicy::Local};
};

// Caller-owned persistent loaded-object registry. Object indexes are stable:
// successful appends only add objects and never reorder existing entries.
// roots/global_scope_objects contain indexes into graph.objects. Mutation is
// performed by the dependency-loader append API; callers may inspect the
// vectors directly but must preserve their index/dedup invariants.
struct Elf32LinkMap {
    Elf32DependencyGraph graph;
    std::vector<Elf32LinkMapRoot> roots;
    std::vector<std::size_t> global_scope_objects;

    [[nodiscard]] std::span<const std::size_t> global_scope() const noexcept {
        return global_scope_objects;
    }
};

struct Elf32LinkMapReclamationOptions {
    // Maximum accumulated graph object count accepted by one planning call.
    std::uint32_t max_objects{};
};

enum class Elf32LinkMapReclamationError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidLinkMap,
    InvalidLiveAnchor,
    ObjectLimitExceeded,
};

struct Elf32LinkMapReclamationPlan {
    // Stable ascending object indexes reachable from persistent roots or
    // caller-supplied live anchors.
    std::vector<std::size_t> reachable_objects;
    // Unreachable objects exactly once in deterministic reverse-postorder.
    // Acyclic requesters precede unreachable dependencies; cycles remain
    // deterministic and once-only.
    std::vector<std::size_t> reclaimable_objects;
};

struct Elf32LinkMapReclamationResult {
    Elf32LinkMapReclamationError error{
        Elf32LinkMapReclamationError::None};
    std::optional<std::size_t> failing_object;
    Elf32LinkMapReclamationPlan plan;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LinkMapReclamationError::None;
    }
};

// Read-only ownership/reachability planning. Persistent roots are always live;
// additional_live_anchors represent borrowed external owners such as active
// libdl handles. Global-scope visibility does not retain ownership.
[[nodiscard]] Elf32LinkMapReclamationResult
plan_elf32_link_map_reclamation(
    const Elf32LinkMap& link_map,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReclamationOptions& options);

[[nodiscard]] const char* to_string(
    Elf32LinkMapReclamationError error) noexcept;

}  // namespace liba32android::elf
