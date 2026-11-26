#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_dynamic.h"
#include "elf/elf32_dynamic_placement.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_linker_strings.h"
#include "elf/elf32_link_map.h"
#include "elf/elf32_load_types.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32DependencyLoadSource {
    std::string identity;
    std::vector<std::uint8_t> image;
};

struct Elf32DependencyLoadOptions {
    std::uint32_t max_objects{};
    std::uint32_t max_depth{};
    std::uint64_t max_dependency_occurrences{};
    std::uint64_t max_image_bytes{};
    std::uint64_t max_total_image_bytes{};
    std::uint32_t max_string_bytes{};
    Elf32DynamicPlacementOptions placement{};
};

enum class Elf32DependencyLoadError : std::uint8_t {
    None = 0,
    InvalidOptions,
    EmptyRootIdentity,
    EmptyRootImage,
    ImageTooLarge,
    TotalImageBytesExceeded,
    TooManyObjects,
    MaxDepthExceeded,
    TooManyDependencyOccurrences,
    DependencyResolveFailed,
    IdentityImageMismatch,
    InvalidImage,
    DependencyNotDynamic,
    PlacementFailed,
    LoadFailed,
    DynamicParseFailed,
    LinkerMetadataFailed,
    LinkerStringFailed,
    RollbackFailed,
    // Kept at the end so feature-016 does not renumber existing public errors.
    InvalidLinkMap,
};

struct Elf32DependencyLoadResult {
    Elf32DependencyLoadError error{Elf32DependencyLoadError::None};
    Elf32DependencyLoadError primary_error{Elf32DependencyLoadError::None};

    Elf32DependencyResolveError dependency_error{Elf32DependencyResolveError::None};
    Elf32DynamicPlacementError placement_error{Elf32DynamicPlacementError::None};
    Elf32LoadError load_error{Elf32LoadError::None};
    Elf32DynamicError dynamic_error{Elf32DynamicError::None};
    Elf32LinkerMetadataError metadata_error{Elf32LinkerMetadataError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};

    std::string failing_identity;
    std::string requested_name;
    // One-shot loads populate graph. Persistent link-map appends mutate the
    // supplied link map in place and leave graph empty.
    Elf32DependencyGraph graph;
    std::optional<std::size_t> root_object_index;
    bool reused_existing_root{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32DependencyLoadError::None;
    }
};

// Load a root plus its transitive dependency graph transactionally. Ordered
// and repeated edges are preserved, equal provider identities reuse one
// object, cycles terminate by reusing known objects, and every mapping owned
// by the call is rolled back if the aggregate operation fails.
[[nodiscard]] Elf32DependencyLoadResult load_elf32_dependency_graph(
    memory::MappedGuestMemory& memory,
    Elf32DependencyLoadSource root,
    Elf32DependencyProvider& provider,
    const Elf32DependencyLoadOptions& options);

// Append one root transactionally into a caller-owned persistent link map.
// Existing identities are reused without remapping; equal identities with
// different bytes fail. max_objects bounds the accumulated object count.
// Failure removes only objects/mappings introduced by this append and preserves
// all pre-existing link-map state. Preflight validates existing identities,
// dependency edges, root records, and exact global-membership consistency before
// mutation. global_scope_objects is maintained in stable insertion order.
[[nodiscard]] Elf32DependencyLoadResult append_elf32_link_map_root(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    Elf32DependencyLoadSource root,
    Elf32DependencyProvider& provider,
    const Elf32DependencyLoadOptions& options,
    Elf32LinkMapRootPolicy root_policy);

enum class Elf32LinkMapRootUpdateError : std::uint8_t {
    None = 0,
    InvalidObject,
    InvalidLinkMap,
};

struct Elf32LinkMapRootUpdateResult {
    Elf32LinkMapRootUpdateError error{Elf32LinkMapRootUpdateError::None};
    bool root_added{};
    bool promoted_global{};
    bool nodelete_set{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LinkMapRootUpdateError::None;
    }
};

// Add or monotonically promote ownership policy for one already-Active object.
// Local never demotes Global and nodelete never clears once set. Global scope is
// maintained in deterministic ascending object-index order.
[[nodiscard]] Elf32LinkMapRootUpdateResult
update_elf32_link_map_root_policy(
    Elf32LinkMap& link_map,
    std::size_t object_index,
    Elf32LinkMapRootPolicy policy,
    bool nodelete);

[[nodiscard]] const char* to_string(
    Elf32LinkMapRootUpdateError error) noexcept;

[[nodiscard]] const char* to_string(Elf32DependencyLoadError error) noexcept;

}  // namespace liba32android::elf
