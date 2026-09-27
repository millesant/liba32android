#include "compat/a32_libdl_close_transaction.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace liba32android::compat {
namespace {

[[nodiscard]] bool callable(std::uint32_t function) noexcept {
    return function != 0U &&
           function != std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] A32LibDlCloseTransactionResult failure(
    A32LibDlCloseTransactionError error,
    std::optional<std::size_t> object_index = std::nullopt) {
    A32LibDlCloseTransactionResult result;
    result.error = error;
    result.object_index = object_index;
    return result;
}

}  // namespace

bool A32LibDlCloseTransaction::options_valid() const noexcept {
    const auto& execution = options_.execution;
    return !handles_.empty() &&
           options_.max_fini_array_entries != 0U &&
           execution.stack_top != 0U &&
           (execution.stack_top & 7U) == 0U &&
           (execution.return_pc & 3U) == 0U &&
           execution.max_instructions_per_call != 0U &&
           execution.service_handler != nullptr &&
           execution.max_service_calls_per_call != 0U;
}

std::optional<std::size_t> A32LibDlCloseTransaction::find_handle(
    std::uint32_t guest_handle) const noexcept {
    for (std::size_t index = 0; index < handles_.size(); ++index) {
        if (handles_[index].refcount != 0U &&
            handles_[index].guest_handle == guest_handle) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> A32LibDlCloseTransaction::dso_for_object(
    std::size_t object_index,
    bool& ambiguous) const noexcept {
    ambiguous = false;
    std::optional<std::uint32_t> explicit_result;
    for (const auto& binding : bindings_) {
        if (binding.object_index != object_index) {
            continue;
        }
        if (binding.dso_handle == 0U) {
            ambiguous = true;
            return std::nullopt;
        }
        if (explicit_result.has_value() &&
            *explicit_result != binding.dso_handle) {
            ambiguous = true;
            return std::nullopt;
        }
        explicit_result = binding.dso_handle;
    }

    bool learned_ambiguous = false;
    const auto learned_result =
        registrations_.dso_for_object(object_index, learned_ambiguous);
    if (learned_ambiguous) {
        ambiguous = true;
        return std::nullopt;
    }
    if (explicit_result.has_value() &&
        learned_result.has_value() &&
        *explicit_result != *learned_result) {
        ambiguous = true;
        return std::nullopt;
    }
    if (explicit_result.has_value()) {
        return explicit_result;
    }
    return learned_result;
}

bool A32LibDlCloseTransaction::registrations_complete(
    std::uint32_t dso_handle) const noexcept {
    for (const auto& record : registrations_.records()) {
        if (record.dso_handle == dso_handle &&
            record.status != A32AeabiAtexitRecordStatus::Complete) {
            return false;
        }
    }
    return true;
}

bool A32LibDlCloseTransaction::matches_state(
    const elf::Elf32LinkMap& link_map,
    std::span<const A32LibDlHandle> handles,
    const elf::Elf32LifecycleState& lifecycle) const noexcept {
    return &link_map == &link_map_ &&
           handles.data() == handles_.data() &&
           handles.size() == handles_.size() &&
           &lifecycle == &lifecycle_;
}

A32LibDlCloseTransactionResult
A32LibDlCloseTransaction::finalize_object(
    memory::GuestMemory& memory,
    std::size_t object_index,
    std::optional<std::uint32_t> nested_stack_top) {
    if (!options_valid()) {
        return failure(A32LibDlCloseTransactionError::InvalidOptions);
    }

    auto execution = options_.execution;
    if (nested_stack_top.has_value()) {
        if (*nested_stack_top == 0U ||
            (*nested_stack_top & 7U) != 0U) {
            return failure(A32LibDlCloseTransactionError::InvalidOptions);
        }
        execution.stack_top = *nested_stack_top;
    }

    if (object_index >= link_map_.graph.objects.size() ||
        !link_map_.object_active(object_index)) {
        return failure(
            A32LibDlCloseTransactionError::InvalidObject,
            object_index);
    }
    if (lifecycle_.objects.size() > link_map_.graph.objects.size()) {
        return failure(
            A32LibDlCloseTransactionError::InvalidLifecycleState,
            object_index);
    }
    lifecycle_.objects.resize(link_map_.graph.objects.size());

    auto& state = lifecycle_.objects[object_index];
    if (state.constructors != elf::Elf32LifecycleObjectStatus::Complete ||
        state.destructors == elf::Elf32LifecycleObjectStatus::Failed) {
        return failure(
            A32LibDlCloseTransactionError::InvalidLifecycleState,
            object_index);
    }
    if (state.destructors == elf::Elf32LifecycleObjectStatus::Complete) {
        A32LibDlCloseTransactionResult result;
        result.object_index = object_index;
        result.outcome = A32LibDlCloseTransactionOutcome::ObjectFinalized;
        return result;
    }

    bool ambiguous = false;
    const auto dso_handle = dso_for_object(object_index, ambiguous);
    if (!dso_handle.has_value() || ambiguous) {
        return failure(
            A32LibDlCloseTransactionError::InvalidBinding,
            object_index);
    }

    const auto& object = link_map_.graph.objects[object_index];
    A32LibDlCloseTransactionResult result;
    result.object_index = object_index;

    if (object.linker_metadata.fini_array.has_value()) {
        const auto decoded = elf::decode_elf32_function_array(
            memory,
            *object.linker_metadata.fini_array,
            elf::Elf32FunctionArrayDecodeOptions{
                .max_entries = options_.max_fini_array_entries,
            });
        if (!decoded) {
            result.decode_error = decoded.error;
            result.error =
                decoded.error ==
                        elf::Elf32FunctionArrayDecodeError::TooManyEntries
                    ? A32LibDlCloseTransactionError::EntryLimitExceeded
                    : A32LibDlCloseTransactionError::DecodeFailed;
            return result;
        }

        std::vector<elf::Elf32FiniCall> calls;
        calls.reserve(decoded.entries.size());
        for (std::size_t reverse = decoded.entries.size();
             reverse > 0U;
             --reverse) {
            const std::size_t array_index = reverse - 1U;
            const std::uint32_t function = decoded.entries[array_index];
            if (!callable(function)) {
                continue;
            }
            calls.push_back(elf::Elf32FiniCall{
                .object_index = object_index,
                .array_index = static_cast<std::uint32_t>(array_index),
                .function = function,
            });
        }

        if (!calls.empty()) {
            const auto executed = elf::execute_elf32_fini_calls(
                memory, calls, execution);
            result.fini_calls_completed += executed.calls_completed;
            if (!executed) {
                state.destructors =
                    elf::Elf32LifecycleObjectStatus::Failed;
                result.error =
                    A32LibDlCloseTransactionError::
                        FiniArrayExecutionFailed;
                result.execution_error = executed.error;
                result.failing_svc_immediate =
                    executed.failing_svc_immediate;
                return result;
            }
        }
    }

    if (!registrations_complete(*dso_handle)) {
        state.destructors = elf::Elf32LifecycleObjectStatus::Failed;
        result.error =
            A32LibDlCloseTransactionError::
                RegisteredFinalizationIncomplete;
        return result;
    }

    if (object.linker_metadata.fini_function.has_value() &&
        callable(*object.linker_metadata.fini_function)) {
        const elf::Elf32FiniCall call{
            .object_index = object_index,
            .array_index =
                std::numeric_limits<std::uint32_t>::max(),
            .function = *object.linker_metadata.fini_function,
        };
        const auto executed = elf::execute_elf32_fini_calls(
            memory,
            std::span<const elf::Elf32FiniCall>{&call, 1U},
            execution);
        result.fini_calls_completed += executed.calls_completed;
        if (!executed) {
            state.destructors =
                elf::Elf32LifecycleObjectStatus::Failed;
            result.error =
                A32LibDlCloseTransactionError::DtFiniExecutionFailed;
            result.execution_error = executed.error;
            result.failing_svc_immediate =
                executed.failing_svc_immediate;
            return result;
        }
    }

    state.destructors = elf::Elf32LifecycleObjectStatus::Complete;
    result.outcome = A32LibDlCloseTransactionOutcome::ObjectFinalized;
    return result;
}

A32LibDlCloseTransactionResult A32LibDlCloseTransaction::close(
    memory::GuestMemory& memory,
    std::uint32_t guest_handle,
    std::optional<std::uint32_t> nested_stack_top) {
    if (!options_valid()) {
        return failure(A32LibDlCloseTransactionError::InvalidOptions);
    }

    const auto slot = find_handle(guest_handle);
    if (!slot.has_value()) {
        return failure(A32LibDlCloseTransactionError::InvalidHandle);
    }

    A32LibDlHandle& handle = handles_[*slot];
    const std::size_t object_index = handle.object_index;
    if (object_index >= link_map_.graph.objects.size() ||
        !link_map_.object_active(object_index)) {
        return failure(
            A32LibDlCloseTransactionError::InvalidObject,
            object_index);
    }

    if (handle.refcount > 1U) {
        --handle.refcount;
        A32LibDlCloseTransactionResult result;
        result.outcome =
            A32LibDlCloseTransactionOutcome::RefcountDecremented;
        result.object_index = object_index;
        return result;
    }

    auto result = finalize_object(
        memory,
        object_index,
        nested_stack_top);
    if (!result) {
        return result;
    }

    handle = {};
    return result;
}

const char* to_string(A32LibDlCloseTransactionError error) noexcept {
    switch (error) {
    case A32LibDlCloseTransactionError::None: return "none";
    case A32LibDlCloseTransactionError::InvalidOptions: return "invalid_options";
    case A32LibDlCloseTransactionError::InvalidHandle: return "invalid_handle";
    case A32LibDlCloseTransactionError::InvalidObject: return "invalid_object";
    case A32LibDlCloseTransactionError::InvalidBinding: return "invalid_binding";
    case A32LibDlCloseTransactionError::InvalidLifecycleState:
        return "invalid_lifecycle_state";
    case A32LibDlCloseTransactionError::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case A32LibDlCloseTransactionError::DecodeFailed: return "decode_failed";
    case A32LibDlCloseTransactionError::FiniArrayExecutionFailed:
        return "fini_array_execution_failed";
    case A32LibDlCloseTransactionError::RegisteredFinalizationIncomplete:
        return "registered_finalization_incomplete";
    case A32LibDlCloseTransactionError::DtFiniExecutionFailed:
        return "dt_fini_execution_failed";
    }
    return "unknown";
}

}  // namespace liba32android::compat
