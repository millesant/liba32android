#include "elf/elf32_dependency_loader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "elf/elf32_load_plan.h"
#include "elf/elf32_loader.h"

namespace liba32android::elf {
namespace {

enum class ObjectState : std::uint8_t {
    Discovered = 0,
    Loading,
    Loaded,
};

[[nodiscard]] Elf32DependencyLoadResult failure(
    Elf32DependencyLoadError error,
    std::string failing_identity = {}) {
    Elf32DependencyLoadResult result;
    result.error = error;
    result.primary_error = error;
    result.failing_identity = std::move(failing_identity);
    return result;
}

[[nodiscard]] bool rollback_load(memory::MappedGuestMemory& memory,
                                 const Elf32LoadResult& load) {
    bool success = true;
    for (auto it = load.segments.rbegin(); it != load.segments.rend(); ++it) {
        if (it->mapping_size > std::numeric_limits<std::size_t>::max()) {
            success = false;
            continue;
        }
        if (!memory.unmap(it->mapping_start,
                          static_cast<std::size_t>(it->mapping_size))) {
            success = false;
        }
    }
    return success;
}

[[nodiscard]] Elf32DependencyLoadResult rollback_failure(
    memory::MappedGuestMemory& memory,
    const std::vector<Elf32LoadResult>& successful_loads,
    Elf32DependencyLoadResult result) {
    bool rollback_ok = true;
    for (auto it = successful_loads.rbegin();
         it != successful_loads.rend(); ++it) {
        if (!rollback_load(memory, *it)) {
            rollback_ok = false;
        }
    }
    if (!rollback_ok) {
        result.primary_error = result.error;
        result.error = Elf32DependencyLoadError::RollbackFailed;
    }
    result.graph = {};
    return result;
}

[[nodiscard]] Elf32DependencyLoadResult load_object(
    memory::MappedGuestMemory& memory,
    Elf32LoadedDependencyObject& object,
    const Elf32DependencyLoadOptions& options,
    bool require_dynamic) {
    const auto plan_result = plan_elf32_load(memory, object.image);
    if (!plan_result) {
        auto result =
            failure(Elf32DependencyLoadError::InvalidImage, object.identity);
        result.load_error = plan_result.error;
        return result;
    }
    if (require_dynamic && plan_result.plan.type != Elf32ImageType::Dynamic) {
        return failure(Elf32DependencyLoadError::DependencyNotDynamic,
                       object.identity);
    }

    Elf32LoadResult load;
    if (plan_result.plan.type == Elf32ImageType::Dynamic) {
        const auto placement =
            place_elf32_dynamic(memory, object.image, options.placement);
        if (!placement) {
            auto result =
                failure(Elf32DependencyLoadError::PlacementFailed,
                        object.identity);
            result.placement_error = placement.error;
            result.load_error = placement.load_error;
            return result;
        }

        Elf32LoadOptions load_options;
        load_options.dynamic_base = placement.dynamic_base;
        load = load_elf32(memory, object.image, load_options);
    } else {
        load = load_elf32(memory, object.image);
    }

    if (!load) {
        auto result =
            failure(Elf32DependencyLoadError::LoadFailed, object.identity);
        result.load_error = load.error;
        return result;
    }

    object.load = std::move(load);
    return {};
}

[[nodiscard]] Elf32DependencyLoadResult inspect_object(
    const memory::MappedGuestMemory& memory,
    Elf32LoadedDependencyObject& object,
    const Elf32DependencyLoadOptions& options) {
    if (!object.load.dynamic_segment.has_value()) {
        return {};
    }

    const auto dynamic =
        parse_elf32_dynamic(memory, *object.load.dynamic_segment);
    if (!dynamic) {
        auto result =
            failure(Elf32DependencyLoadError::DynamicParseFailed,
                    object.identity);
        result.dynamic_error = dynamic.error;
        return result;
    }
    object.dynamic_entries = dynamic.entries;

    const auto metadata =
        build_elf32_linker_metadata(memory, object.load.load_bias,
                                    object.dynamic_entries);
    if (!metadata) {
        auto result =
            failure(Elf32DependencyLoadError::LinkerMetadataFailed,
                    object.identity);
        result.metadata_error = metadata.error;
        return result;
    }
    object.linker_metadata = metadata.metadata;

    const auto strings = build_elf32_linker_strings(
        memory, object.linker_metadata,
        Elf32LinkerStringOptions{
            .max_string_bytes = options.max_string_bytes,
        });
    if (!strings) {
        auto result =
            failure(Elf32DependencyLoadError::LinkerStringFailed,
                    object.identity);
        result.string_error = strings.error;
        return result;
    }
    object.linker_strings = strings.strings;
    return {};
}

struct GraphLoadContext {
    memory::MappedGuestMemory& memory;
    Elf32DependencyProvider& provider;
    const Elf32DependencyLoadOptions& options;
    Elf32DependencyGraph& graph;

    std::unordered_map<std::string, std::size_t> object_indices;
    std::vector<ObjectState> states;
    std::vector<Elf32LoadResult> successful_loads;
    std::uint64_t dependency_occurrences{};
    std::uint64_t total_image_bytes{};

    [[nodiscard]] Elf32DependencyLoadResult process_object(
        std::size_t index,
        std::uint64_t depth,
        bool require_dynamic) {
        if (states[index] == ObjectState::Loaded ||
            states[index] == ObjectState::Loading) {
            return {};
        }

        if (depth > options.max_depth) {
            return failure(Elf32DependencyLoadError::MaxDepthExceeded,
                           graph.objects[index].identity);
        }

        states[index] = ObjectState::Loading;

        auto load_result =
            load_object(memory, graph.objects[index], options, require_dynamic);
        if (!load_result) {
            return load_result;
        }
        successful_loads.push_back(graph.objects[index].load);

        auto inspect_result =
            inspect_object(memory, graph.objects[index], options);
        if (!inspect_result) {
            return inspect_result;
        }

        const std::size_t direct_size =
            graph.objects[index].linker_strings.needed.size();
        if (direct_size > std::numeric_limits<std::uint32_t>::max()) {
            return failure(
                Elf32DependencyLoadError::TooManyDependencyOccurrences,
                graph.objects[index].identity);
        }

        const std::uint64_t direct_occurrences =
            static_cast<std::uint64_t>(direct_size);
        if (direct_occurrences >
            options.max_dependency_occurrences - dependency_occurrences) {
            return failure(
                Elf32DependencyLoadError::TooManyDependencyOccurrences,
                graph.objects[index].identity);
        }

        if (direct_size == 0) {
            states[index] = ObjectState::Loaded;
            return {};
        }

        const std::uint64_t remaining_total =
            options.max_total_image_bytes - total_image_bytes;
        const auto resolved = resolve_elf32_dependencies(
            graph.objects[index].linker_strings,
            provider,
            Elf32DependencyResolveOptions{
                .max_dependencies = static_cast<std::uint32_t>(direct_size),
                .max_image_bytes = options.max_image_bytes,
                .max_total_image_bytes = remaining_total,
                .requester_identity = graph.objects[index].identity,
            });
        if (!resolved) {
            auto result =
                failure(Elf32DependencyLoadError::DependencyResolveFailed,
                        graph.objects[index].identity);
            result.dependency_error = resolved.error;
            return result;
        }

        dependency_occurrences += direct_occurrences;

        std::uint64_t acquired_bytes = 0;
        for (const auto& dependency : resolved.dependencies.ordered) {
            const std::uint64_t image_bytes =
                static_cast<std::uint64_t>(dependency.image.size());
            if (image_bytes > remaining_total - acquired_bytes) {
                return failure(
                    Elf32DependencyLoadError::TotalImageBytesExceeded,
                    graph.objects[index].identity);
            }
            acquired_bytes += image_bytes;
        }
        total_image_bytes += acquired_bytes;

        for (const auto& dependency : resolved.dependencies.ordered) {
            const auto known = object_indices.find(dependency.identity);
            std::size_t target_index =
                known == object_indices.end() ? graph.objects.size()
                                              : known->second;

            if (target_index != graph.objects.size()) {
                if (graph.objects[target_index].image != dependency.image) {
                    auto result =
                        failure(Elf32DependencyLoadError::IdentityImageMismatch,
                                dependency.identity);
                    result.requested_name = dependency.requested_name;
                    return result;
                }

                graph.objects[index].dependencies.push_back(
                    Elf32DependencyEdge{
                        .requested_name = dependency.requested_name,
                        .target_object = target_index,
                    });

                if (states[target_index] == ObjectState::Discovered) {
                    auto nested =
                        process_object(target_index, depth + 1U, true);
                    if (!nested) {
                        if (nested.requested_name.empty()) {
                            nested.requested_name =
                                dependency.requested_name;
                        }
                        return nested;
                    }
                }
                continue;
            }

            if (graph.objects.size() >=
                static_cast<std::size_t>(options.max_objects)) {
                auto result =
                    failure(Elf32DependencyLoadError::TooManyObjects,
                            dependency.identity);
                result.requested_name = dependency.requested_name;
                return result;
            }

            target_index = graph.objects.size();
            Elf32LoadedDependencyObject child;
            child.identity = dependency.identity;
            child.image = dependency.image;
            graph.objects.push_back(std::move(child));
            states.push_back(ObjectState::Discovered);
            object_indices.emplace(
                graph.objects[target_index].identity, target_index);

            graph.objects[index].dependencies.push_back(
                Elf32DependencyEdge{
                    .requested_name = dependency.requested_name,
                    .target_object = target_index,
                });

            auto nested = process_object(target_index, depth + 1U, true);
            if (!nested) {
                if (nested.requested_name.empty()) {
                    nested.requested_name = dependency.requested_name;
                }
                return nested;
            }
        }

        states[index] = ObjectState::Loaded;
        return {};
    }
};

}  // namespace

Elf32DependencyLoadResult load_elf32_dependency_graph(
    memory::MappedGuestMemory& memory,
    Elf32DependencyLoadSource root,
    Elf32DependencyProvider& provider,
    const Elf32DependencyLoadOptions& options) {
    if (options.max_objects == 0) {
        return failure(Elf32DependencyLoadError::InvalidOptions, root.identity);
    }
    if (root.identity.empty()) {
        return failure(Elf32DependencyLoadError::EmptyRootIdentity);
    }
    if (root.image.empty()) {
        return failure(Elf32DependencyLoadError::EmptyRootImage, root.identity);
    }

    const std::uint64_t root_image_bytes =
        static_cast<std::uint64_t>(root.image.size());
    if (root_image_bytes > options.max_image_bytes) {
        return failure(Elf32DependencyLoadError::ImageTooLarge, root.identity);
    }
    if (root_image_bytes > options.max_total_image_bytes) {
        return failure(Elf32DependencyLoadError::TotalImageBytesExceeded,
                       root.identity);
    }

    Elf32DependencyGraph graph;
    GraphLoadContext context{
        .memory = memory,
        .provider = provider,
        .options = options,
        .graph = graph,
        .total_image_bytes = root_image_bytes,
    };

    Elf32LoadedDependencyObject root_object;
    root_object.identity = std::move(root.identity);
    root_object.image = std::move(root.image);
    context.graph.objects.push_back(std::move(root_object));
    context.object_indices.emplace(context.graph.objects.front().identity, 0U);
    context.states.push_back(ObjectState::Discovered);

    auto result = context.process_object(0, 0, false);
    if (!result) {
        return rollback_failure(memory, context.successful_loads,
                                std::move(result));
    }

    result.graph = std::move(graph);
    result.root_object_index = 0U;
    return result;
}

Elf32DependencyLoadResult append_elf32_link_map_root(
    memory::MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    Elf32DependencyLoadSource root,
    Elf32DependencyProvider& provider,
    const Elf32DependencyLoadOptions& options,
    Elf32LinkMapRootPolicy root_policy) {
    if (options.max_objects == 0) {
        return failure(Elf32DependencyLoadError::InvalidOptions, root.identity);
    }
    if (root.identity.empty()) {
        return failure(Elf32DependencyLoadError::EmptyRootIdentity);
    }
    if (root.image.empty()) {
        return failure(Elf32DependencyLoadError::EmptyRootImage, root.identity);
    }

    const std::uint64_t root_image_bytes =
        static_cast<std::uint64_t>(root.image.size());
    if (root_image_bytes > options.max_image_bytes) {
        return failure(Elf32DependencyLoadError::ImageTooLarge, root.identity);
    }
    if (root_image_bytes > options.max_total_image_bytes) {
        return failure(Elf32DependencyLoadError::TotalImageBytesExceeded,
                       root.identity);
    }
    if (link_map.graph.objects.size() >
        static_cast<std::size_t>(options.max_objects)) {
        return failure(Elf32DependencyLoadError::TooManyObjects, root.identity);
    }

    // The link map is caller-owned, so validate every structural invariant
    // consumed by append before any mutation. Retired slots remain stable but
    // do not participate in active identity reuse.
    const std::size_t existing_object_count = link_map.graph.objects.size();
    if (!link_map.object_states.empty() &&
        link_map.object_states.size() != existing_object_count) {
        return failure(Elf32DependencyLoadError::InvalidLinkMap, root.identity);
    }
    const auto object_active =
        [&](std::size_t object_index) {
            return link_map.object_states.empty() ||
                   link_map.object_states[object_index] ==
                       Elf32LinkMapObjectState::Active;
        };
    if (!link_map.object_states.empty()) {
        for (const auto state : link_map.object_states) {
            if (state != Elf32LinkMapObjectState::Active &&
                state != Elf32LinkMapObjectState::Retired) {
                return failure(
                    Elf32DependencyLoadError::InvalidLinkMap, root.identity);
            }
        }
    }

    std::unordered_map<std::string, std::size_t> object_indices;
    object_indices.reserve(existing_object_count + 1U);
    for (std::size_t index = 0; index < existing_object_count; ++index) {
        const auto& object = link_map.graph.objects[index];
        if (object.identity.empty()) {
            return failure(Elf32DependencyLoadError::InvalidLinkMap,
                           object.identity);
        }
        if (object_active(index) &&
            !object_indices.emplace(object.identity, index).second) {
            return failure(Elf32DependencyLoadError::InvalidLinkMap,
                           object.identity);
        }
        for (const auto& edge : object.dependencies) {
            if (edge.requested_name.empty() ||
                edge.target_object >= existing_object_count ||
                (object_active(index) &&
                 !object_active(edge.target_object))) {
                return failure(Elf32DependencyLoadError::InvalidLinkMap,
                               object.identity);
            }
        }
    }

    std::vector<std::uint8_t> root_seen(existing_object_count, 0);
    std::vector<std::uint8_t> required_global(existing_object_count, 0);
    for (std::size_t index = 0; index < existing_object_count; ++index) {
        if (object_active(index) &&
            link_map.graph.objects[index].linker_metadata.global) {
            required_global[index] = 1;
        }
    }
    for (const auto& record : link_map.roots) {
        if (record.object_index >= existing_object_count ||
            !object_active(record.object_index) ||
            root_seen[record.object_index] != 0 ||
            (record.policy != Elf32LinkMapRootPolicy::Local &&
             record.policy != Elf32LinkMapRootPolicy::Global)) {
            return failure(Elf32DependencyLoadError::InvalidLinkMap);
        }
        root_seen[record.object_index] = 1;
        if (record.policy == Elf32LinkMapRootPolicy::Global) {
            required_global[record.object_index] = 1;
        }
    }

    std::vector<std::uint8_t> global_seen(existing_object_count, 0);
    bool has_previous_global = false;
    std::size_t previous_global = 0;
    for (const std::size_t object_index : link_map.global_scope_objects) {
        if (object_index >= existing_object_count ||
            !object_active(object_index) ||
            global_seen[object_index] != 0 ||
            required_global[object_index] == 0 ||
            (has_previous_global && object_index <= previous_global)) {
            return failure(Elf32DependencyLoadError::InvalidLinkMap);
        }
        global_seen[object_index] = 1;
        previous_global = object_index;
        has_previous_global = true;
    }
    for (std::size_t index = 0; index < required_global.size(); ++index) {
        if (required_global[index] != global_seen[index]) {
            return failure(Elf32DependencyLoadError::InvalidLinkMap,
                           link_map.graph.objects[index].identity);
        }
    }

    const auto record_root =
        [&](std::size_t object_index) {
            for (auto& record : link_map.roots) {
                if (record.object_index == object_index) {
                    if (root_policy == Elf32LinkMapRootPolicy::Global) {
                        record.policy = Elf32LinkMapRootPolicy::Global;
                    }
                    return;
                }
            }
            link_map.roots.push_back(Elf32LinkMapRoot{
                .object_index = object_index,
                .policy = root_policy,
            });
        };

    const auto record_global =
        [&](std::size_t object_index) {
            if (global_seen.size() < link_map.graph.objects.size()) {
                global_seen.resize(link_map.graph.objects.size(), 0);
            }
            if (global_seen[object_index] == 0) {
                global_seen[object_index] = 1;
                const auto position = std::lower_bound(
                    link_map.global_scope_objects.begin(),
                    link_map.global_scope_objects.end(),
                    object_index);
                link_map.global_scope_objects.insert(position, object_index);
            }
        };

    const auto known = object_indices.find(root.identity);
    if (known != object_indices.end()) {
        const std::size_t root_index = known->second;
        if (link_map.graph.objects[root_index].image != root.image) {
            return failure(Elf32DependencyLoadError::IdentityImageMismatch,
                           root.identity);
        }
        if (link_map.object_states.empty()) {
            link_map.object_states.assign(
                existing_object_count, Elf32LinkMapObjectState::Active);
        }
        record_root(root_index);
        if (root_policy == Elf32LinkMapRootPolicy::Global ||
            link_map.graph.objects[root_index].linker_metadata.global) {
            record_global(root_index);
        }
        Elf32DependencyLoadResult result;
        result.root_object_index = root_index;
        result.reused_existing_root = true;
        return result;
    }

    if (link_map.graph.objects.size() >=
        static_cast<std::size_t>(options.max_objects)) {
        return failure(Elf32DependencyLoadError::TooManyObjects, root.identity);
    }

    const std::size_t initial_object_count = link_map.graph.objects.size();
    const std::size_t root_index = initial_object_count;

    Elf32LoadedDependencyObject root_object;
    root_object.identity = std::move(root.identity);
    root_object.image = std::move(root.image);
    link_map.graph.objects.push_back(std::move(root_object));
    object_indices.emplace(link_map.graph.objects[root_index].identity,
                           root_index);

    std::vector<ObjectState> states(
        initial_object_count, ObjectState::Loaded);
    states.push_back(ObjectState::Discovered);

    GraphLoadContext context{
        .memory = memory,
        .provider = provider,
        .options = options,
        .graph = link_map.graph,
        .object_indices = std::move(object_indices),
        .states = std::move(states),
        .total_image_bytes = root_image_bytes,
    };

    auto result = context.process_object(root_index, 0, false);
    if (!result) {
        result = rollback_failure(
            memory, context.successful_loads, std::move(result));
        link_map.graph.objects.resize(initial_object_count);
        return result;
    }

    if (link_map.object_states.empty()) {
        link_map.object_states.assign(
            initial_object_count, Elf32LinkMapObjectState::Active);
    }
    link_map.object_states.resize(
        link_map.graph.objects.size(), Elf32LinkMapObjectState::Active);

    record_root(root_index);
    for (std::size_t object_index = initial_object_count;
         object_index < link_map.graph.objects.size(); ++object_index) {
        const bool caller_global =
            object_index == root_index &&
            root_policy == Elf32LinkMapRootPolicy::Global;
        if (caller_global ||
            link_map.graph.objects[object_index].linker_metadata.global) {
            record_global(object_index);
        }
    }
    result.root_object_index = root_index;
    result.reused_existing_root = false;
    return result;
}

[[nodiscard]] static Elf32LinkMapReclamationResult
plan_elf32_link_map_reclamation_impl(
    const Elf32LinkMap& link_map,
    std::optional<std::size_t> root_to_release,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReclamationOptions& options) {
    Elf32LinkMapReclamationResult result;
    if (options.max_objects == 0U) {
        result.error = Elf32LinkMapReclamationError::InvalidOptions;
        return result;
    }

    const std::size_t object_count = link_map.graph.objects.size();
    if (object_count > static_cast<std::size_t>(options.max_objects)) {
        result.error = Elf32LinkMapReclamationError::ObjectLimitExceeded;
        return result;
    }
    if (!link_map.object_states.empty() &&
        link_map.object_states.size() != object_count) {
        result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
        return result;
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
                result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
                result.failing_object = object_index;
                return result;
            }
        }
    }

    std::unordered_map<std::string, std::size_t> active_identities;
    active_identities.reserve(object_count);
    for (std::size_t object_index = 0;
         object_index < object_count;
         ++object_index) {
        const auto& object = link_map.graph.objects[object_index];
        if (object.identity.empty()) {
            result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
            result.failing_object = object_index;
            return result;
        }
        if (object_active(object_index) &&
            !active_identities.emplace(
                object.identity, object_index).second) {
            result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
            result.failing_object = object_index;
            return result;
        }
        for (const auto& edge : object.dependencies) {
            if (edge.requested_name.empty() ||
                edge.target_object >= object_count ||
                (object_active(object_index) &&
                 !object_active(edge.target_object))) {
                result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
                result.failing_object = object_index;
                return result;
            }
        }
    }

    std::vector<std::uint8_t> root_seen(object_count, 0U);
    bool released_root_found = false;
    for (const auto& root : link_map.roots) {
        if (root.object_index >= object_count ||
            !object_active(root.object_index) ||
            root_seen[root.object_index] != 0U ||
            (root.policy != Elf32LinkMapRootPolicy::Local &&
             root.policy != Elf32LinkMapRootPolicy::Global)) {
            result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
            if (root.object_index < object_count) {
                result.failing_object = root.object_index;
            }
            return result;
        }
        root_seen[root.object_index] = 1U;
        if (root_to_release.has_value() &&
            root.object_index == *root_to_release) {
            released_root_found = true;
        }
    }
    if (root_to_release.has_value() && !released_root_found) {
        result.error = Elf32LinkMapReclamationError::InvalidRoot;
        result.failing_object = *root_to_release;
        return result;
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
        bool has_previous_global = false;
        std::size_t previous_global = 0U;
        for (const std::size_t object_index : link_map.global_scope_objects) {
            if (object_index >= object_count ||
                !object_active(object_index) ||
                global_seen[object_index] != 0U ||
                required_global[object_index] == 0U ||
                (has_previous_global && object_index <= previous_global)) {
                result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
                if (object_index < object_count) {
                    result.failing_object = object_index;
                }
                return result;
            }
            global_seen[object_index] = 1U;
            previous_global = object_index;
            has_previous_global = true;
        }
        for (std::size_t object_index = 0;
             object_index < object_count;
             ++object_index) {
            if (required_global[object_index] != global_seen[object_index]) {
                result.error = Elf32LinkMapReclamationError::InvalidLinkMap;
                result.failing_object = object_index;
                return result;
            }
        }
    }

    for (const std::size_t object_index : additional_live_anchors) {
        if (object_index >= object_count || !object_active(object_index)) {
            result.error = Elf32LinkMapReclamationError::InvalidLiveAnchor;
            result.failing_object = object_index;
            return result;
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

    for (const auto& root : link_map.roots) {
        if (root_to_release.has_value() &&
            root.object_index == *root_to_release) {
            continue;
        }
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

    result.plan.reachable_objects.reserve(object_count);
    for (std::size_t object_index = 0;
         object_index < object_count;
         ++object_index) {
        if (reachable[object_index] != 0U) {
            result.plan.reachable_objects.push_back(object_index);
        }
    }

    struct VisitFrame {
        std::size_t object_index{};
        std::size_t next_dependency{};
    };

    std::vector<std::uint8_t> visit_state(object_count, 0U);
    std::vector<VisitFrame> stack;
    std::vector<std::size_t> postorder;
    stack.reserve(object_count);
    postorder.reserve(
        object_count - result.plan.reachable_objects.size());

    // Descending seeds make independent reclaimable objects appear in stable
    // ascending object-index order after the final postorder reversal.
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
                if (reachable[target] != 0U ||
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

    result.plan.reclaimable_objects.assign(
        postorder.rbegin(), postorder.rend());
    return result;
}

Elf32LinkMapReclamationResult plan_elf32_link_map_reclamation(
    const Elf32LinkMap& link_map,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReclamationOptions& options) {
    return plan_elf32_link_map_reclamation_impl(
        link_map,
        std::nullopt,
        additional_live_anchors,
        options);
}

Elf32LinkMapReclamationResult plan_elf32_link_map_root_release(
    const Elf32LinkMap& link_map,
    std::size_t root_object_index,
    std::span<const std::size_t> additional_live_anchors,
    const Elf32LinkMapReclamationOptions& options) {
    return plan_elf32_link_map_reclamation_impl(
        link_map,
        root_object_index,
        additional_live_anchors,
        options);
}

const char* to_string(Elf32LinkMapReclamationError error) noexcept {
    switch (error) {
    case Elf32LinkMapReclamationError::None:
        return "none";
    case Elf32LinkMapReclamationError::InvalidOptions:
        return "invalid_options";
    case Elf32LinkMapReclamationError::InvalidLinkMap:
        return "invalid_link_map";
    case Elf32LinkMapReclamationError::InvalidLiveAnchor:
        return "invalid_live_anchor";
    case Elf32LinkMapReclamationError::ObjectLimitExceeded:
        return "object_limit_exceeded";
    case Elf32LinkMapReclamationError::InvalidRoot:
        return "invalid_root";
    }
    return "unknown";
}

const char* to_string(Elf32DependencyLoadError error) noexcept {
    switch (error) {
    case Elf32DependencyLoadError::None:
        return "none";
    case Elf32DependencyLoadError::InvalidOptions:
        return "invalid_options";
    case Elf32DependencyLoadError::InvalidLinkMap:
        return "invalid_link_map";
    case Elf32DependencyLoadError::EmptyRootIdentity:
        return "empty_root_identity";
    case Elf32DependencyLoadError::EmptyRootImage:
        return "empty_root_image";
    case Elf32DependencyLoadError::ImageTooLarge:
        return "image_too_large";
    case Elf32DependencyLoadError::TotalImageBytesExceeded:
        return "total_image_bytes_exceeded";
    case Elf32DependencyLoadError::TooManyObjects:
        return "too_many_objects";
    case Elf32DependencyLoadError::MaxDepthExceeded:
        return "max_depth_exceeded";
    case Elf32DependencyLoadError::TooManyDependencyOccurrences:
        return "too_many_dependency_occurrences";
    case Elf32DependencyLoadError::DependencyResolveFailed:
        return "dependency_resolve_failed";
    case Elf32DependencyLoadError::IdentityImageMismatch:
        return "identity_image_mismatch";
    case Elf32DependencyLoadError::InvalidImage:
        return "invalid_image";
    case Elf32DependencyLoadError::DependencyNotDynamic:
        return "dependency_not_dynamic";
    case Elf32DependencyLoadError::PlacementFailed:
        return "placement_failed";
    case Elf32DependencyLoadError::LoadFailed:
        return "load_failed";
    case Elf32DependencyLoadError::DynamicParseFailed:
        return "dynamic_parse_failed";
    case Elf32DependencyLoadError::LinkerMetadataFailed:
        return "linker_metadata_failed";
    case Elf32DependencyLoadError::LinkerStringFailed:
        return "linker_string_failed";
    case Elf32DependencyLoadError::RollbackFailed:
        return "rollback_failed";
    }
    return "unknown";
}

}  // namespace liba32android::elf
