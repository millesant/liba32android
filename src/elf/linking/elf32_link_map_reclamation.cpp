#include "elf/elf32_link_map_reclamation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace liba32android::elf {
namespace {

struct MappingSnapshot {
    std::size_t object_index{};
    std::size_t segment_index{};
    std::uint32_t start{};
    std::size_t length{};
    std::vector<std::uint8_t> bytes;
    std::vector<memory::MemoryPermission> page_permissions;
};

[[nodiscard]] Elf32LinkMapReleaseResult failure(
    Elf32LinkMapReleaseError error,
    std::optional<std::size_t> released_root,
    std::optional<std::size_t> failing_object = std::nullopt,
    std::optional<std::size_t> failing_segment = std::nullopt) {
    Elf32LinkMapReleaseResult result;
    result.error = error;
    result.released_root = released_root;
    result.failing_object = failing_object;
    result.failing_segment = failing_segment;
    return result;
}

[[nodiscard]] bool valid_lifecycle_state(
    const Elf32LifecycleObjectState& state) noexcept {
    return
        (state.constructors == Elf32LifecycleObjectStatus::Pending &&
         state.destructors == Elf32LifecycleObjectStatus::Pending) ||
        (state.constructors == Elf32LifecycleObjectStatus::Complete &&
         state.destructors == Elf32LifecycleObjectStatus::Complete);
}

[[nodiscard]] bool snapshot_page(
    const memory::MappedGuestMemory& memory,
    std::uint32_t address,
    memory::MemoryPermission permissions,
    std::span<std::uint8_t> output) {
    return memory::has_permission(
               permissions, memory::MemoryPermission::Read) &&
           memory.read(address, output);
}

[[nodiscard]] bool restore_snapshot(
    memory::MappedGuestMemory& memory,
    const MappingSnapshot& snapshot) {
    const std::size_t page_size = memory.page_size();
    if (page_size == 0U || snapshot.length == 0U ||
        (snapshot.length % page_size) != 0U ||
        snapshot.page_permissions.size() != snapshot.length / page_size) {
        return false;
    }

    bool any_mapped = false;
    bool any_unmapped = false;
    for (std::size_t offset = 0; offset < snapshot.length;
         offset += page_size) {
        const auto page =
            snapshot.start + static_cast<std::uint32_t>(offset);
        if (memory.is_mapped(page)) {
            any_mapped = true;
        } else {
            any_unmapped = true;
        }
    }
    if (any_mapped && any_unmapped) {
        return false;
    }

    const auto writable =
        memory::MemoryPermission::Read | memory::MemoryPermission::Write;
    if (any_mapped) {
        if (!memory.protect(snapshot.start, snapshot.length, writable)) {
            return false;
        }
    } else if (!memory.map(snapshot.start, snapshot.length, writable)) {
        return false;
    }

    if (!memory.write(snapshot.start, snapshot.bytes)) {
        return false;
    }

    std::size_t run_begin = 0U;
    while (run_begin < snapshot.page_permissions.size()) {
        const auto permissions = snapshot.page_permissions[run_begin];
        std::size_t run_end = run_begin + 1U;
        while (run_end < snapshot.page_permissions.size() &&
               snapshot.page_permissions[run_end] == permissions) {
            ++run_end;
        }

        const std::size_t byte_offset = run_begin * page_size;
        const std::size_t byte_length = (run_end - run_begin) * page_size;
        const std::uint64_t address64 =
            static_cast<std::uint64_t>(snapshot.start) + byte_offset;
        if (address64 > std::numeric_limits<std::uint32_t>::max() ||
            !memory.protect(
                static_cast<std::uint32_t>(address64),
                byte_length,
                permissions)) {
            return false;
        }
        run_begin = run_end;
    }
    return true;
}

}  // namespace

[[nodiscard]] static Elf32LinkMapReleaseResult reclaim_link_map_impl(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    const Elf32LifecycleState& lifecycle,
    std::optional<std::size_t> root_to_release,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReleaseOptions& options) {
    if (options.max_objects == 0U ||
        options.max_segments == 0U ||
        options.max_snapshot_bytes == 0U) {
        return failure(
            Elf32LinkMapReleaseError::InvalidOptions, root_to_release);
    }

    const std::size_t object_count = link_map.graph.objects.size();
    if (object_count > static_cast<std::size_t>(options.max_objects)) {
        return failure(
            Elf32LinkMapReleaseError::ObjectLimitExceeded,
            root_to_release);
    }
    if (lifecycle.objects.size() != object_count ||
        (!link_map.object_states.empty() &&
         link_map.object_states.size() != object_count)) {
        return failure(
            Elf32LinkMapReleaseError::InvalidLinkMap,
            root_to_release);
    }

    const auto object_active =
        [&](std::size_t object_index) {
            return link_map.object_states.empty() ||
                   link_map.object_states[object_index] ==
                       Elf32LinkMapObjectState::Active;
        };

    if (!link_map.object_states.empty()) {
        for (std::size_t object_index = 0;
             object_index < object_count;
             ++object_index) {
            const auto state = link_map.object_states[object_index];
            if (state != Elf32LinkMapObjectState::Active &&
                state != Elf32LinkMapObjectState::Retired) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidLinkMap,
                    root_to_release,
                    object_index);
            }
        }
    }

    std::vector<std::string_view> active_identities;
    active_identities.reserve(object_count);
    for (std::size_t object_index = 0;
         object_index < object_count;
         ++object_index) {
        const auto& object = link_map.graph.objects[object_index];
        if (object.identity.empty()) {
            return failure(
                Elf32LinkMapReleaseError::InvalidLinkMap,
                root_to_release,
                object_index);
        }
        if (object_active(object_index)) {
            const std::string_view identity{object.identity};
            if (std::find(
                    active_identities.begin(),
                    active_identities.end(),
                    identity) != active_identities.end()) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidLinkMap,
                    root_to_release,
                    object_index);
            }
            active_identities.push_back(identity);
        }
        for (const auto& edge : object.dependencies) {
            if (edge.requested_name.empty() ||
                edge.target_object >= object_count ||
                (object_active(object_index) &&
                 !object_active(edge.target_object))) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidLinkMap,
                    root_to_release,
                    object_index);
            }
        }
    }

    std::vector<std::uint8_t> root_seen(object_count, 0U);
    std::optional<std::size_t> released_root_record;
    for (std::size_t record_index = 0;
         record_index < link_map.roots.size();
         ++record_index) {
        const auto& root = link_map.roots[record_index];
        if (root.object_index >= object_count ||
            !object_active(root.object_index) ||
            root_seen[root.object_index] != 0U ||
            (root.policy != Elf32LinkMapRootPolicy::Local &&
             root.policy != Elf32LinkMapRootPolicy::Global)) {
            return failure(
                Elf32LinkMapReleaseError::InvalidLinkMap,
                root_to_release,
                root.object_index < object_count
                    ? std::optional<std::size_t>{root.object_index}
                    : std::nullopt);
        }
        root_seen[root.object_index] = 1U;
        if (root_to_release.has_value() &&
            root.object_index == *root_to_release) {
            released_root_record = record_index;
        }
    }
    if (root_to_release.has_value() &&
        !released_root_record.has_value()) {
        return failure(
            Elf32LinkMapReleaseError::InvalidRoot, root_to_release);
    }

    {
        std::vector<std::uint8_t> required_global(object_count, 0U);
        for (std::size_t object_index = 0;
             object_index < object_count;
             ++object_index) {
            if (object_active(object_index) &&
                link_map.graph.objects[object_index].linker_metadata.global) {
                required_global[object_index] = 1U;
            }
        }
        for (const auto& root : link_map.roots) {
            if (root.policy == Elf32LinkMapRootPolicy::Global) {
                required_global[root.object_index] = 1U;
            }
        }

        std::vector<std::uint8_t> global_seen(object_count, 0U);
        bool has_previous = false;
        std::size_t previous = 0U;
        for (const std::size_t object_index : link_map.global_scope_objects) {
            if (object_index >= object_count ||
                !object_active(object_index) ||
                global_seen[object_index] != 0U ||
                required_global[object_index] == 0U ||
                (has_previous && object_index <= previous)) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidLinkMap,
                    root_to_release,
                    object_index < object_count
                        ? std::optional<std::size_t>{object_index}
                        : std::nullopt);
            }
            global_seen[object_index] = 1U;
            previous = object_index;
            has_previous = true;
        }
        for (std::size_t object_index = 0;
             object_index < object_count;
             ++object_index) {
            if (required_global[object_index] != global_seen[object_index]) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidLinkMap,
                    root_to_release,
                    object_index);
            }
        }
    }

    for (const std::size_t object_index : additional_live_anchors) {
        if (object_index >= object_count || !object_active(object_index)) {
            return failure(
                Elf32LinkMapReleaseError::InvalidLiveAnchor,
                root_to_release,
                object_index);
        }
    }

    std::vector<Elf32LinkMapRoot> new_roots;
    new_roots.reserve(
        link_map.roots.size() -
        (released_root_record.has_value() ? 1U : 0U));
    for (std::size_t record_index = 0;
         record_index < link_map.roots.size();
         ++record_index) {
        if (!released_root_record.has_value() ||
            record_index != *released_root_record) {
            new_roots.push_back(link_map.roots[record_index]);
        }
    }

    std::vector<std::uint8_t> reachable(object_count, 0U);
    std::vector<std::size_t> work;
    work.reserve(object_count);
    const auto add_anchor =
        [&](std::size_t object_index) {
            if (reachable[object_index] == 0U) {
                reachable[object_index] = 1U;
                work.push_back(object_index);
            }
        };
    for (const auto& root : new_roots) {
        add_anchor(root.object_index);
    }
    for (const std::size_t object_index : additional_live_anchors) {
        add_anchor(object_index);
    }
    while (!work.empty()) {
        const std::size_t object_index = work.back();
        work.pop_back();
        for (const auto& edge :
             link_map.graph.objects[object_index].dependencies) {
            if (reachable[edge.target_object] == 0U) {
                reachable[edge.target_object] = 1U;
                work.push_back(edge.target_object);
            }
        }
    }

    struct VisitFrame {
        std::size_t object_index{};
        std::size_t next_dependency{};
    };
    std::vector<std::uint8_t> visit_state(object_count, 0U);
    std::vector<VisitFrame> stack;
    std::vector<std::size_t> reclaimable;
    std::vector<std::size_t> postorder;
    stack.reserve(object_count);
    postorder.reserve(object_count);

    for (std::size_t seed = object_count; seed > 0U; --seed) {
        const std::size_t object_index = seed - 1U;
        if (!object_active(object_index) ||
            reachable[object_index] != 0U ||
            visit_state[object_index] != 0U) {
            continue;
        }
        visit_state[object_index] = 1U;
        stack.push_back(VisitFrame{.object_index = object_index});
        while (!stack.empty()) {
            VisitFrame& frame = stack.back();
            const auto& dependencies =
                link_map.graph.objects[frame.object_index].dependencies;
            bool descended = false;
            while (frame.next_dependency < dependencies.size()) {
                const std::size_t target =
                    dependencies[frame.next_dependency++].target_object;
                if (!object_active(target) ||
                    reachable[target] != 0U ||
                    visit_state[target] != 0U) {
                    continue;
                }
                visit_state[target] = 1U;
                stack.push_back(VisitFrame{.object_index = target});
                descended = true;
                break;
            }
            if (descended) {
                continue;
            }
            visit_state[frame.object_index] = 2U;
            postorder.push_back(frame.object_index);
            stack.pop_back();
        }
    }
    reclaimable.assign(postorder.rbegin(), postorder.rend());

    for (const std::size_t object_index : reclaimable) {
        if (!valid_lifecycle_state(lifecycle.objects[object_index])) {
            return failure(
                Elf32LinkMapReleaseError::InvalidLifecycleState,
                root_to_release,
                object_index);
        }
    }

    std::vector<Elf32LinkMapObjectState> new_states;
    if (link_map.object_states.empty()) {
        new_states.assign(object_count, Elf32LinkMapObjectState::Active);
    } else {
        new_states = link_map.object_states;
    }
    for (const std::size_t object_index : reclaimable) {
        new_states[object_index] = Elf32LinkMapObjectState::Retired;
    }

    std::vector<std::uint8_t> global_required(object_count, 0U);
    for (std::size_t object_index = 0;
         object_index < object_count;
         ++object_index) {
        if (new_states[object_index] == Elf32LinkMapObjectState::Active &&
            link_map.graph.objects[object_index].linker_metadata.global) {
            global_required[object_index] = 1U;
        }
    }
    for (const auto& root : new_roots) {
        if (root.policy == Elf32LinkMapRootPolicy::Global) {
            global_required[root.object_index] = 1U;
        }
    }
    std::vector<std::size_t> new_global_scope;
    new_global_scope.reserve(link_map.global_scope_objects.size());
    for (std::size_t object_index = 0;
         object_index < object_count;
         ++object_index) {
        if (global_required[object_index] != 0U) {
            new_global_scope.push_back(object_index);
        }
    }

    const std::size_t page_size = memory.page_size();
    if (page_size == 0U) {
        return failure(
            Elf32LinkMapReleaseError::InvalidMapping, root_to_release);
    }

    std::vector<MappingSnapshot> snapshots;
    snapshots.reserve(options.max_segments);
    std::uint64_t snapshot_bytes = 0U;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;

    for (const std::size_t object_index : reclaimable) {
        const auto& segments = link_map.graph.objects[object_index].load.segments;
        for (std::size_t segment_index = 0;
             segment_index < segments.size();
             ++segment_index) {
            if (snapshots.size() >= options.max_segments) {
                return failure(
                    Elf32LinkMapReleaseError::SegmentLimitExceeded,
                    root_to_release,
                    object_index,
                    segment_index);
            }
            const auto& segment = segments[segment_index];
            if (segment.mapping_size == 0U ||
                segment.mapping_size >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max()) ||
                (segment.mapping_start % page_size) != 0U ||
                (segment.mapping_size % page_size) != 0U) {
                return failure(
                    Elf32LinkMapReleaseError::InvalidMapping,
                    root_to_release,
                    object_index,
                    segment_index);
            }
            const std::uint64_t end =
                static_cast<std::uint64_t>(segment.mapping_start) +
                segment.mapping_size;
            if (end > memory::MappedGuestMemory::kAddressSpaceSize ||
                segment.mapping_size >
                    options.max_snapshot_bytes - snapshot_bytes) {
                return failure(
                    end > memory::MappedGuestMemory::kAddressSpaceSize
                        ? Elf32LinkMapReleaseError::InvalidMapping
                        : Elf32LinkMapReleaseError::SnapshotByteLimitExceeded,
                    root_to_release,
                    object_index,
                    segment_index);
            }

            for (const auto& range : ranges) {
                if (static_cast<std::uint64_t>(segment.mapping_start) <
                        range.second &&
                    range.first < end) {
                    return failure(
                        Elf32LinkMapReleaseError::InvalidMapping,
                        root_to_release,
                        object_index,
                        segment_index);
                }
            }
            ranges.push_back({
                static_cast<std::uint64_t>(segment.mapping_start), end});

            MappingSnapshot snapshot;
            snapshot.object_index = object_index;
            snapshot.segment_index = segment_index;
            snapshot.start = segment.mapping_start;
            snapshot.length =
                static_cast<std::size_t>(segment.mapping_size);
            snapshot.bytes.resize(snapshot.length);
            snapshot.page_permissions.reserve(snapshot.length / page_size);

            for (std::size_t offset = 0;
                 offset < snapshot.length;
                 offset += page_size) {
                const std::uint64_t page64 =
                    static_cast<std::uint64_t>(snapshot.start) + offset;
                if (page64 > std::numeric_limits<std::uint32_t>::max()) {
                    return failure(
                        Elf32LinkMapReleaseError::InvalidMapping,
                        root_to_release,
                        object_index,
                        segment_index);
                }
                const auto page = static_cast<std::uint32_t>(page64);
                if (!memory.is_mapped(page)) {
                    return failure(
                        Elf32LinkMapReleaseError::InvalidMapping,
                        root_to_release,
                        object_index,
                        segment_index);
                }
                const auto permissions = memory.permissions(page);
                snapshot.page_permissions.push_back(permissions);
                if (!snapshot_page(
                        memory,
                        page,
                        permissions,
                        std::span<std::uint8_t>{
                            snapshot.bytes.data() + offset, page_size})) {
                    return failure(
                        Elf32LinkMapReleaseError::SnapshotFailed,
                        root_to_release,
                        object_index,
                        segment_index);
                }
            }

            snapshot_bytes += segment.mapping_size;
            snapshots.push_back(std::move(snapshot));
        }
    }

    std::size_t touched = 0U;
    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        ++touched;
        const auto& snapshot = snapshots[index];
        if (memory.unmap(snapshot.start, snapshot.length)) {
            continue;
        }

        bool rollback_ok = true;
        for (std::size_t reverse = touched; reverse > 0U; --reverse) {
            if (!restore_snapshot(memory, snapshots[reverse - 1U])) {
                rollback_ok = false;
            }
        }
        auto result = failure(
            rollback_ok
                ? Elf32LinkMapReleaseError::UnmapFailed
                : Elf32LinkMapReleaseError::RollbackFailed,
            root_to_release,
            snapshot.object_index,
            snapshot.segment_index);
        result.mappings_unmapped = index;
        return result;
    }

    link_map.roots = std::move(new_roots);
    link_map.global_scope_objects = std::move(new_global_scope);
    link_map.object_states = std::move(new_states);

    Elf32LinkMapReleaseResult result;
    result.released_root = root_to_release;
    result.reclaimed_objects = std::move(reclaimable);
    result.mappings_unmapped = snapshots.size();
    return result;
}

Elf32LinkMapReleaseResult reclaim_elf32_link_map_unreachable(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    const Elf32LifecycleState& lifecycle,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReleaseOptions& options) {
    return reclaim_link_map_impl(
        memory,
        link_map,
        lifecycle,
        std::nullopt,
        additional_live_anchors,
        options);
}

Elf32LinkMapReleaseResult release_elf32_link_map_root(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    const Elf32LifecycleState& lifecycle,
    std::size_t root_object_index,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReleaseOptions& options) {
    return reclaim_link_map_impl(
        memory,
        link_map,
        lifecycle,
        root_object_index,
        additional_live_anchors,
        options);
}

const char* to_string(Elf32LinkMapReleaseError error) noexcept {
    switch (error) {
    case Elf32LinkMapReleaseError::None: return "none";
    case Elf32LinkMapReleaseError::InvalidOptions: return "invalid_options";
    case Elf32LinkMapReleaseError::InvalidLinkMap: return "invalid_link_map";
    case Elf32LinkMapReleaseError::InvalidRoot: return "invalid_root";
    case Elf32LinkMapReleaseError::InvalidLiveAnchor:
        return "invalid_live_anchor";
    case Elf32LinkMapReleaseError::ObjectLimitExceeded:
        return "object_limit_exceeded";
    case Elf32LinkMapReleaseError::InvalidLifecycleState:
        return "invalid_lifecycle_state";
    case Elf32LinkMapReleaseError::SegmentLimitExceeded:
        return "segment_limit_exceeded";
    case Elf32LinkMapReleaseError::SnapshotByteLimitExceeded:
        return "snapshot_byte_limit_exceeded";
    case Elf32LinkMapReleaseError::InvalidMapping: return "invalid_mapping";
    case Elf32LinkMapReleaseError::SnapshotFailed: return "snapshot_failed";
    case Elf32LinkMapReleaseError::UnmapFailed: return "unmap_failed";
    case Elf32LinkMapReleaseError::RollbackFailed: return "rollback_failed";
    }
    return "unknown";
}

}  // namespace liba32android::elf
