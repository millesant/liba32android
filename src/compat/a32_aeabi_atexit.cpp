#include "compat/a32_aeabi_atexit.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "cpu/a32_cpu.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

runtime::A32HostServiceDisposition A32AeabiAtexitService::handle(
    memory::GuestMemory&,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32AeabiAtexitSvcImmediate) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }

    if (record_count_ >= records_.size()) {
        regs[0] = 0xffffffffU;
        return runtime::A32HostServiceDisposition::Handled;
    }

    records_[record_count_] = A32AeabiAtexitRecord{
        .object = regs[0],
        .destructor = regs[1],
        .dso_handle = regs[2],
    };
    ++record_count_;
    regs[0] = 0U;
    return runtime::A32HostServiceDisposition::Handled;
}

namespace {

[[nodiscard]] bool valid_finalize_options(
    const A32AeabiFinalizeOptions& options) noexcept {
    return options.stack_top != 0U &&
           (options.stack_top & 7U) == 0U &&
           (options.return_pc & 3U) == 0U &&
           options.max_instructions_per_call != 0U &&
           options.max_callbacks != 0U;
}

[[nodiscard]] bool selected_record(
    const A32AeabiAtexitRecord& record,
    std::optional<std::uint32_t> dso_handle) noexcept {
    return !dso_handle.has_value() ||
           record.dso_handle == *dso_handle;
}

[[nodiscard]] A32AeabiFinalizeResult finalize_failure(
    A32AeabiFinalizeError error,
    std::size_t callbacks_completed = 0U,
    std::optional<std::size_t> failing_record = std::nullopt,
    std::optional<cpu::ExecutionResult> cpu_result = std::nullopt) {
    A32AeabiFinalizeResult result;
    result.error = error;
    result.callbacks_completed = callbacks_completed;
    result.failing_record = failing_record;
    result.cpu_result = std::move(cpu_result);
    return result;
}

}  // namespace

A32AeabiFinalizeResult A32AeabiAtexitService::finalize(
    memory::GuestMemory& memory,
    std::optional<std::uint32_t> dso_handle,
    const A32AeabiFinalizeOptions& options) {
    if (!valid_finalize_options(options)) {
        return finalize_failure(A32AeabiFinalizeError::InvalidOptions);
    }

    std::uint32_t pending_callbacks = 0U;
    for (std::size_t reverse = record_count_; reverse > 0U; --reverse) {
        const std::size_t index = reverse - 1U;
        const A32AeabiAtexitRecord& record = records_[index];
        if (!selected_record(record, dso_handle)) {
            continue;
        }
        if (record.status == A32AeabiAtexitRecordStatus::Failed) {
            return finalize_failure(
                A32AeabiFinalizeError::InvalidRecordState,
                0U,
                index);
        }
        if (record.status == A32AeabiAtexitRecordStatus::Pending) {
            ++pending_callbacks;
            if (pending_callbacks > options.max_callbacks) {
                return finalize_failure(
                    A32AeabiFinalizeError::CallbackLimitExceeded);
            }
        }
    }

    A32AeabiFinalizeResult result;
    for (std::size_t reverse = record_count_; reverse > 0U; --reverse) {
        const std::size_t index = reverse - 1U;
        A32AeabiAtexitRecord& record = records_[index];
        if (!selected_record(record, dso_handle) ||
            record.status == A32AeabiAtexitRecordStatus::Complete) {
            continue;
        }

        const bool thumb = (record.destructor & 1U) != 0U;
        const std::uint32_t entry_pc = record.destructor & ~1U;
        if (record.destructor == 0U ||
            record.destructor ==
                std::numeric_limits<std::uint32_t>::max() ||
            entry_pc == options.return_pc ||
            (!thumb && (entry_pc & 3U) != 0U)) {
            record.status = A32AeabiAtexitRecordStatus::Failed;
            return finalize_failure(
                A32AeabiFinalizeError::InvalidFunctionAddress,
                result.callbacks_completed,
                index);
        }

        cpu::ExecutionRequest request{};
        request.instruction_set =
            thumb ? cpu::InstructionSet::Thumb : cpu::InstructionSet::Arm;
        request.entry_pc = entry_pc;
        request.regs[0] = record.object;
        request.regs[13] = options.stack_top;
        request.regs[14] = options.return_pc | (thumb ? 1U : 0U);
        request.instruction_count = options.max_instructions_per_call;
        request.stop_pc = options.return_pc;

        auto cpu_result = cpu::execute(memory, request);
        if (cpu_result.memory_fault) {
            record.status = A32AeabiAtexitRecordStatus::Failed;
            return finalize_failure(
                A32AeabiFinalizeError::MemoryFault,
                result.callbacks_completed,
                index,
                std::move(cpu_result));
        }
        if (cpu_result.exception_raised) {
            record.status = A32AeabiAtexitRecordStatus::Failed;
            return finalize_failure(
                A32AeabiFinalizeError::CpuException,
                result.callbacks_completed,
                index,
                std::move(cpu_result));
        }
        if (!cpu_result.stop_pc_reached) {
            record.status = A32AeabiAtexitRecordStatus::Failed;
            return finalize_failure(
                A32AeabiFinalizeError::InstructionLimitExceeded,
                result.callbacks_completed,
                index,
                std::move(cpu_result));
        }

        record.status = A32AeabiAtexitRecordStatus::Complete;
        ++result.callbacks_completed;
    }
    return result;
}

const char* to_string(A32AeabiFinalizeError error) noexcept {
    switch (error) {
    case A32AeabiFinalizeError::None: return "none";
    case A32AeabiFinalizeError::InvalidOptions:
        return "invalid_options";
    case A32AeabiFinalizeError::CallbackLimitExceeded:
        return "callback_limit_exceeded";
    case A32AeabiFinalizeError::InvalidRecordState:
        return "invalid_record_state";
    case A32AeabiFinalizeError::InvalidFunctionAddress:
        return "invalid_function_address";
    case A32AeabiFinalizeError::CpuException:
        return "cpu_exception";
    case A32AeabiFinalizeError::MemoryFault:
        return "memory_fault";
    case A32AeabiFinalizeError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::compat
