#include "compat/a32_libdl_unload_transaction.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace liba32android::compat {
namespace {

[[nodiscard]] A32LibDlUnloadTransactionResult failure(
    A32LibDlUnloadTransactionError error,
    std::optional<std::size_t> object_index = std::nullopt) {
    A32LibDlUnloadTransactionResult result;
    result.error = error;
    result.object_index = object_index;
    return result;
}

}  // namespace

bool A32LibDlUnloadTransaction::options_valid() const noexcept {
    return !handles_.empty() &&
           options_.planning.max_objects != 0U &&
           options_.reclamation.max_objects >=
               options_.planning.max_objects &&
           options_.reclamation.max_segments != 0U &&
           options_.reclamation.max_snapshot_bytes != 0U &&
           lifecycle_.objects.size() == link_map_.graph.objects.size() &&
           finalizer_.matches_state(link_map_, handles_, lifecycle_);
}

std::optional<std::size_t> A32LibDlUnloadTransaction::find_handle(
    std::uint32_t guest_handle) const noexcept {
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        if (handles_[index].refcount != 0U &&
            handles_[index].guest_handle == guest_handle) {
            return index;
        }
    }
    return std::nullopt;
}

bool A32LibDlUnloadTransaction::collect_other_live_anchors(
    std::size_t closing_slot,
    std::vector<std::size_t>& anchors,
    std::optional<std::size_t>& failing_object) const {
    anchors.clear();
    anchors.reserve(handles_.size());
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        if (index == closing_slot || handles_[index].refcount == 0U) {
            continue;
        }
        const std::size_t object_index = handles_[index].object_index;
        if (object_index >= link_map_.graph.objects.size() ||
            !link_map_.object_active(object_index)) {
            failing_object = object_index;
            return false;
        }
        anchors.push_back(object_index);
    }
    return true;
}

A32LibDlUnloadTransactionResult A32LibDlUnloadTransaction::close(
    memory::MappedGuestMemory& memory,
    std::uint32_t guest_handle,
    std::optional<std::uint32_t> nested_stack_top) {
    if (!options_valid()) {
        return failure(A32LibDlUnloadTransactionError::InvalidOptions);
    }

    const auto slot = find_handle(guest_handle);
    if (!slot.has_value()) {
        return failure(A32LibDlUnloadTransactionError::InvalidHandle);
    }

    A32LibDlHandle& handle = handles_[*slot];
    const std::size_t object_index = handle.object_index;
    if (object_index >= link_map_.graph.objects.size() ||
        !link_map_.object_active(object_index)) {
        return failure(
            A32LibDlUnloadTransactionError::InvalidObject,
            object_index);
    }

    if (handle.refcount > 1U) {
        --handle.refcount;
        A32LibDlUnloadTransactionResult result;
        result.object_index = object_index;
        result.outcome =
            A32LibDlUnloadTransactionOutcome::RefcountDecremented;
        return result;
    }

    const elf::Elf32LinkMapRoot* exact_root = nullptr;
    for (const auto& root : link_map_.roots) {
        if (root.object_index != object_index) {
            continue;
        }
        if (exact_root != nullptr) {
            return failure(
                A32LibDlUnloadTransactionError::InvalidOwnershipState,
                object_index);
        }
        exact_root = &root;
    }
    if (exact_root == nullptr) {
        return failure(
            A32LibDlUnloadTransactionError::InvalidOwnershipState,
            object_index);
    }
    if (exact_root->nodelete) {
        handle = {};
        A32LibDlUnloadTransactionResult result;
        result.object_index = object_index;
        result.outcome =
            A32LibDlUnloadTransactionOutcome::NodeleteRetained;
        return result;
    }

    std::vector<std::size_t> live_anchors;
    std::optional<std::size_t> failing_anchor;
    if (!collect_other_live_anchors(
            *slot, live_anchors, failing_anchor)) {
        auto result = failure(
            A32LibDlUnloadTransactionError::InvalidOwnershipState,
            object_index);
        result.failing_object = failing_anchor;
        return result;
    }

    const auto baseline = elf::plan_elf32_link_map_reclamation(
        link_map_,
        live_anchors,
        options_.planning);
    if (!baseline) {
        auto result = failure(
            A32LibDlUnloadTransactionError::OwnershipPlanFailed,
            object_index);
        result.planning_error = baseline.error;
        result.failing_object = baseline.failing_object;
        return result;
    }
    if (!baseline.plan.reclaimable_objects.empty()) {
        auto result = failure(
            A32LibDlUnloadTransactionError::PreexistingUnownedObjects,
            object_index);
        result.failing_object =
            baseline.plan.reclaimable_objects.front();
        return result;
    }

    const auto planned = elf::plan_elf32_link_map_root_release(
        link_map_,
        object_index,
        live_anchors,
        options_.planning);
    if (!planned) {
        auto result = failure(
            A32LibDlUnloadTransactionError::RootReleasePlanFailed,
            object_index);
        result.planning_error = planned.error;
        result.failing_object = planned.failing_object;
        return result;
    }

    A32LibDlUnloadTransactionResult result;
    result.object_index = object_index;
    result.teardown_objects = planned.plan.reclaimable_objects;

    for (const std::size_t teardown_object :
         result.teardown_objects) {
        if (teardown_object >= lifecycle_.objects.size()) {
            result.error =
                A32LibDlUnloadTransactionError::FinalizationFailed;
            result.finalization_error =
                A32LibDlCloseTransactionError::InvalidLifecycleState;
            result.failing_object = teardown_object;
            return result;
        }

        const bool already_complete =
            lifecycle_.objects[teardown_object].destructors ==
            elf::Elf32LifecycleObjectStatus::Complete;
        const auto finalized = finalizer_.finalize_object(
            memory,
            teardown_object,
            nested_stack_top);
        if (!finalized) {
            result.error =
                A32LibDlUnloadTransactionError::FinalizationFailed;
            result.finalization_error = finalized.error;
            result.failing_object = finalized.object_index;
            return result;
        }
        if (!already_complete) {
            ++result.objects_completed;
        }
    }

    const auto released = elf::release_elf32_link_map_root(
        memory,
        link_map_,
        lifecycle_,
        object_index,
        live_anchors,
        options_.reclamation);
    if (!released) {
        result.error =
            A32LibDlUnloadTransactionError::ReclamationFailed;
        result.reclamation_error = released.error;
        result.failing_object = released.failing_object;
        return result;
    }

    result.mappings_unmapped = released.mappings_unmapped;
    for (const std::size_t reclaimed_object :
         released.reclaimed_objects) {
        finalizer_.forget_learned_binding(reclaimed_object);
    }
    handle = {};
    result.outcome =
        result.teardown_objects.empty()
            ? A32LibDlUnloadTransactionOutcome::RootReleased
            : A32LibDlUnloadTransactionOutcome::ObjectsUnloaded;
    return result;
}

const char* to_string(
    A32LibDlUnloadTransactionError error) noexcept {
    switch (error) {
    case A32LibDlUnloadTransactionError::None:
        return "none";
    case A32LibDlUnloadTransactionError::InvalidOptions:
        return "invalid_options";
    case A32LibDlUnloadTransactionError::InvalidHandle:
        return "invalid_handle";
    case A32LibDlUnloadTransactionError::InvalidObject:
        return "invalid_object";
    case A32LibDlUnloadTransactionError::InvalidOwnershipState:
        return "invalid_ownership_state";
    case A32LibDlUnloadTransactionError::OwnershipPlanFailed:
        return "ownership_plan_failed";
    case A32LibDlUnloadTransactionError::PreexistingUnownedObjects:
        return "preexisting_unowned_objects";
    case A32LibDlUnloadTransactionError::RootReleasePlanFailed:
        return "root_release_plan_failed";
    case A32LibDlUnloadTransactionError::FinalizationFailed:
        return "finalization_failed";
    case A32LibDlUnloadTransactionError::ReclamationFailed:
        return "reclamation_failed";
    }
    return "unknown";
}

}  // namespace liba32android::compat
