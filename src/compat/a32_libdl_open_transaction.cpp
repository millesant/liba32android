#include "compat/a32_libdl_open_transaction.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace liba32android::compat {
namespace {

[[nodiscard]] A32LibDlOpenTransactionResult failure(
    A32LibDlOpenTransactionError error,
    std::optional<std::size_t> object_index = std::nullopt) {
    A32LibDlOpenTransactionResult result;
    result.error = error;
    result.object_index = object_index;
    return result;
}

}  // namespace

bool A32LibDlOpenTransaction::options_valid() const noexcept {
    const auto& symbols = options_.relocation.symbols;
    const auto& execution = options_.lifecycle.execution;
    if (handles_.empty() ||
        options_.handle_base == 0U ||
        (options_.handle_base & 0x3U) != 0U ||
        options_.load.max_objects == 0U ||
        options_.load.max_depth == 0U ||
        options_.load.max_image_bytes == 0U ||
        options_.load.max_total_image_bytes == 0U ||
        options_.load.max_string_bytes == 0U ||
        options_.relocation.max_relocations == 0U ||
        symbols.max_symbols == 0U ||
        symbols.max_scope_objects == 0U ||
        symbols.max_name_bytes == 0U ||
        options_.lifecycle.max_objects == 0U ||
        execution.stack_top == 0U ||
        (execution.stack_top & 7U) != 0U ||
        (execution.return_pc & 3U) != 0U ||
        execution.max_instructions_per_call == 0U ||
        options_.reclamation.max_objects == 0U ||
        options_.reclamation.max_segments == 0U ||
        options_.reclamation.max_snapshot_bytes == 0U ||
        lifecycle_.objects.size() > link_map_.graph.objects.size()) {
        return false;
    }

    const std::uint64_t last_handle =
        static_cast<std::uint64_t>(options_.handle_base) +
        (handles_.size() - 1U) * 4ULL;
    return last_handle <= std::numeric_limits<std::uint32_t>::max() &&
           last_handle != kA32RtldNext;
}

std::optional<std::size_t> A32LibDlOpenTransaction::find_active_object(
    std::string_view name,
    bool& ambiguous) const noexcept {
    ambiguous = false;
    std::optional<std::size_t> result;
    for (std::size_t index = 0;
         index < link_map_.graph.objects.size();
         ++index) {
        if (!link_map_.object_active(index)) {
            continue;
        }
        const auto& object = link_map_.graph.objects[index];
        const bool match =
            object.identity == name ||
            (object.linker_strings.soname.has_value() &&
             *object.linker_strings.soname == name);
        if (!match) {
            continue;
        }
        if (result.has_value()) {
            ambiguous = true;
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

std::optional<std::size_t> A32LibDlOpenTransaction::find_active_identity(
    std::string_view identity) const noexcept {
    std::optional<std::size_t> result;
    for (std::size_t index = 0;
         index < link_map_.graph.objects.size();
         ++index) {
        if (!link_map_.object_active(index) ||
            link_map_.graph.objects[index].identity != identity) {
            continue;
        }
        if (result.has_value()) {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

bool A32LibDlOpenTransaction::root_record_exists(
    std::size_t object_index) const noexcept {
    return std::any_of(
        link_map_.roots.begin(),
        link_map_.roots.end(),
        [&](const elf::Elf32LinkMapRoot& root) {
            return root.object_index == object_index;
        });
}

bool A32LibDlOpenTransaction::can_acquire_handle(
    std::optional<std::size_t> object_index,
    A32LibDlOpenTransactionError& error) const noexcept {
    if (object_index.has_value()) {
        for (const auto& handle : handles_) {
            if (handle.refcount == 0U ||
                handle.object_index != *object_index) {
                continue;
            }
            if (handle.refcount ==
                std::numeric_limits<std::uint32_t>::max()) {
                error = A32LibDlOpenTransactionError::HandleRefcountOverflow;
                return false;
            }
            return true;
        }
    }

    for (const auto& handle : handles_) {
        if (handle.refcount == 0U) {
            return true;
        }
    }
    error = A32LibDlOpenTransactionError::HandleTableExhausted;
    return false;
}

std::uint32_t A32LibDlOpenTransaction::acquire_handle(
    std::size_t object_index) noexcept {
    for (auto& handle : handles_) {
        if (handle.refcount != 0U &&
            handle.object_index == object_index) {
            if (handle.refcount ==
                std::numeric_limits<std::uint32_t>::max()) {
                return 0U;
            }
            ++handle.refcount;
            return handle.guest_handle;
        }
    }

    for (std::size_t index = 0; index < handles_.size(); ++index) {
        auto& handle = handles_[index];
        if (handle.refcount != 0U) {
            continue;
        }
        const std::uint64_t value =
            static_cast<std::uint64_t>(options_.handle_base) +
            index * 4ULL;
        if (value == 0U ||
            value > std::numeric_limits<std::uint32_t>::max() ||
            value == kA32RtldNext) {
            return 0U;
        }
        handle = A32LibDlHandle{
            .guest_handle = static_cast<std::uint32_t>(value),
            .object_index = object_index,
            .refcount = 1U,
        };
        return handle.guest_handle;
    }
    return 0U;
}

std::vector<std::size_t>
A32LibDlOpenTransaction::live_handle_anchors() const {
    std::vector<std::size_t> anchors;
    anchors.reserve(handles_.size());
    for (const auto& handle : handles_) {
        if (handle.refcount == 0U ||
            handle.object_index >= link_map_.graph.objects.size() ||
            !link_map_.object_active(handle.object_index)) {
            continue;
        }
        anchors.push_back(handle.object_index);
    }
    return anchors;
}

A32LibDlOpenTransactionResult
A32LibDlOpenTransaction::cleanup_failure(
    A32LibDlOpenTransactionResult result,
    std::size_t root_object_index) {
    const auto anchors = live_handle_anchors();
    const auto cleaned = elf::release_elf32_link_map_root(
        memory_,
        link_map_,
        lifecycle_,
        root_object_index,
        anchors,
        options_.reclamation);
    if (!cleaned) {
        result.error = A32LibDlOpenTransactionError::CleanupFailed;
        result.cleanup_error = cleaned.error;
        result.failing_object = cleaned.failing_object;
    }
    return result;
}

A32LibDlOpenTransactionResult A32LibDlOpenTransaction::open(
    std::string_view requested_name,
    std::optional<std::uint32_t> nested_stack_top) {
    if (!options_valid()) {
        return failure(A32LibDlOpenTransactionError::InvalidOptions);
    }
    if (requested_name.empty()) {
        return failure(A32LibDlOpenTransactionError::InvalidName);
    }
    if (nested_stack_top.has_value() &&
        (*nested_stack_top == 0U ||
         (*nested_stack_top & 7U) != 0U)) {
        return failure(A32LibDlOpenTransactionError::InvalidOptions);
    }

    bool ambiguous = false;
    std::optional<std::size_t> object_index =
        find_active_object(requested_name, ambiguous);
    if (ambiguous) {
        return failure(
            A32LibDlOpenTransactionError::AmbiguousResidentObject);
    }

    std::size_t initial_object_count = link_map_.graph.objects.size();
    bool root_added = false;

    if (!object_index.has_value()) {
        const auto provided = provider_.resolve(
            requested_name, options_.load.max_image_bytes);
        if (!provided) {
            auto result = failure(
                provided.error == elf::Elf32DependencyProviderError::NotFound
                    ? A32LibDlOpenTransactionError::DependencyNotFound
                    : A32LibDlOpenTransactionError::ProviderFailed);
            result.provider_error = provided.error;
            return result;
        }
        if (provided.source.identity.empty() ||
            provided.source.image.empty() ||
            static_cast<std::uint64_t>(provided.source.image.size()) >
                options_.load.max_image_bytes) {
            auto result = failure(
                A32LibDlOpenTransactionError::InvalidProviderResult);
            result.provider_error = provided.error;
            return result;
        }

        const auto known_identity =
            find_active_identity(provided.source.identity);
        A32LibDlOpenTransactionError capacity_error{
            A32LibDlOpenTransactionError::None};
        if (!can_acquire_handle(known_identity, capacity_error)) {
            return failure(capacity_error, known_identity);
        }

        const bool root_preexisted =
            known_identity.has_value() &&
            root_record_exists(*known_identity);
        auto appended = elf::append_elf32_link_map_root(
            memory_,
            link_map_,
            elf::Elf32DependencyLoadSource{
                .identity = std::move(provided.source.identity),
                .image = std::move(provided.source.image),
            },
            provider_,
            options_.load,
            elf::Elf32LinkMapRootPolicy::Local);
        if (!appended || !appended.root_object_index.has_value()) {
            auto result =
                failure(A32LibDlOpenTransactionError::AppendFailed);
            result.load_error = appended.error;
            result.failing_object = appended.root_object_index;
            return result;
        }

        object_index = *appended.root_object_index;
        root_added = !root_preexisted;
        lifecycle_.objects.resize(link_map_.graph.objects.size());

        auto rollback_before_constructors =
            [&](A32LibDlOpenTransactionResult result) {
                result.object_index = object_index;
                result.root_added = root_added;
                result.objects_appended =
                    link_map_.graph.objects.size() - initial_object_count;
                if (root_added) {
                    return cleanup_failure(
                        std::move(result), *object_index);
                }
                return result;
            };

        auto relocation_options = options_.relocation;
        relocation_options.symbols.global_scope_objects =
            link_map_.global_scope();
        for (std::size_t index = initial_object_count;
             index < link_map_.graph.objects.size();
             ++index) {
            const auto relocated = elf::apply_elf32_combined_relocations(
                memory_,
                link_map_.graph,
                index,
                relocation_options);
            if (!relocated) {
                auto result = failure(
                    A32LibDlOpenTransactionError::RelocationFailed,
                    *object_index);
                result.relocation_error = relocated.error;
                result.failing_object = index;
                return rollback_before_constructors(std::move(result));
            }
        }

        for (std::size_t index = initial_object_count;
             index < link_map_.graph.objects.size();
             ++index) {
            const auto sealed = elf::seal_elf32_gnu_relro(
                memory_,
                link_map_.graph.objects[index].load,
                options_.relro);
            if (!sealed) {
                auto result = failure(
                    A32LibDlOpenTransactionError::RelroFailed,
                    *object_index);
                result.relro_error = sealed.error;
                result.failing_object = index;
                return rollback_before_constructors(std::move(result));
            }
        }
    } else {
        A32LibDlOpenTransactionError capacity_error{
            A32LibDlOpenTransactionError::None};
        if (!can_acquire_handle(object_index, capacity_error)) {
            return failure(capacity_error, object_index);
        }
        lifecycle_.objects.resize(link_map_.graph.objects.size());
    }

    auto& state = lifecycle_.objects[*object_index];
    if (state.constructors == elf::Elf32LifecycleObjectStatus::Failed ||
        state.destructors != elf::Elf32LifecycleObjectStatus::Pending) {
        auto result = failure(
            A32LibDlOpenTransactionError::InvalidResidentLifecycle,
            object_index);
        result.root_added = root_added;
        result.objects_appended =
            link_map_.graph.objects.size() - initial_object_count;
        return result;
    }

    if (state.constructors == elf::Elf32LifecycleObjectStatus::Pending) {
        auto lifecycle_options = options_.lifecycle;
        if (nested_stack_top.has_value()) {
            lifecycle_options.execution.stack_top = *nested_stack_top;
        }
        const auto initialized = elf::run_elf32_persistent_constructors(
            memory_,
            link_map_.graph,
            lifecycle_,
            *object_index,
            lifecycle_options);
        if (!initialized) {
            auto result = failure(
                A32LibDlOpenTransactionError::ConstructorFailed,
                object_index);
            result.lifecycle_error = initialized.error;
            result.failing_object = initialized.failing_object;
            result.root_added = root_added;
            result.objects_appended =
                link_map_.graph.objects.size() - initial_object_count;
            result.retained_after_failure = true;
            return result;
        }
    }

    const std::uint32_t guest_handle = acquire_handle(*object_index);
    if (guest_handle == 0U) {
        auto result = failure(
            A32LibDlOpenTransactionError::HandleTableExhausted,
            object_index);
        result.root_added = root_added;
        result.objects_appended =
            link_map_.graph.objects.size() - initial_object_count;
        result.retained_after_failure = true;
        return result;
    }

    A32LibDlOpenTransactionResult result;
    result.guest_handle = guest_handle;
    result.object_index = object_index;
    result.root_added = root_added;
    result.objects_appended =
        link_map_.graph.objects.size() - initial_object_count;
    return result;
}

const char* to_string(
    A32LibDlOpenTransactionError error) noexcept {
    switch (error) {
    case A32LibDlOpenTransactionError::None: return "none";
    case A32LibDlOpenTransactionError::InvalidOptions:
        return "invalid_options";
    case A32LibDlOpenTransactionError::InvalidName:
        return "invalid_name";
    case A32LibDlOpenTransactionError::AmbiguousResidentObject:
        return "ambiguous_resident_object";
    case A32LibDlOpenTransactionError::InvalidResidentLifecycle:
        return "invalid_resident_lifecycle";
    case A32LibDlOpenTransactionError::HandleTableExhausted:
        return "handle_table_exhausted";
    case A32LibDlOpenTransactionError::HandleRefcountOverflow:
        return "handle_refcount_overflow";
    case A32LibDlOpenTransactionError::DependencyNotFound:
        return "dependency_not_found";
    case A32LibDlOpenTransactionError::ProviderFailed:
        return "provider_failed";
    case A32LibDlOpenTransactionError::InvalidProviderResult:
        return "invalid_provider_result";
    case A32LibDlOpenTransactionError::AppendFailed:
        return "append_failed";
    case A32LibDlOpenTransactionError::RelocationFailed:
        return "relocation_failed";
    case A32LibDlOpenTransactionError::RelroFailed:
        return "relro_failed";
    case A32LibDlOpenTransactionError::ConstructorFailed:
        return "constructor_failed";
    case A32LibDlOpenTransactionError::CleanupFailed:
        return "cleanup_failed";
    }
    return "unknown";
}

}  // namespace liba32android::compat
