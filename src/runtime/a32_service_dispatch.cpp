#include "runtime/a32_service_dispatch.h"

#include <cstddef>
#include <cstdint>

#include "memory/guest_memory.h"

namespace liba32android::runtime {
namespace {

[[nodiscard]] A32ServiceDispatchResult failure(
    A32ServiceDispatchResult result,
    A32ServiceDispatchError error,
    std::optional<std::uint32_t> failing_svc = std::nullopt) {
    result.error = error;
    result.failing_svc_immediate = failing_svc;
    return result;
}

}  // namespace

A32ServiceDispatchResult execute_a32_with_services(
    cpu::A32Executor& executor,
    cpu::ExecutionRequest request,
    A32HostServiceHandler& handler,
    std::size_t max_service_calls) {
    memory::GuestMemory& memory = executor.memory();
    A32ServiceDispatchResult result;
    result.regs = request.regs;
    result.regs[15] = request.entry_pc;
    result.cpsr = request.initial_cpsr.value_or(
        request.instruction_set == cpu::InstructionSet::Thumb
            ? 0x30U
            : 0x10U);

    std::size_t remaining_instructions = request.instruction_count;
    const auto stop_pc = request.stop_pc;

    while (true) {
        request.instruction_count = remaining_instructions;
        const cpu::ExecutionResult cpu_result = executor.execute(request);

        if (cpu_result.instructions_executed > remaining_instructions) {
            return failure(result, A32ServiceDispatchError::CpuException);
        }
        remaining_instructions -= cpu_result.instructions_executed;
        result.instructions_executed += cpu_result.instructions_executed;
        result.regs = cpu_result.regs;
        result.cpsr = cpu_result.cpsr;
        result.stop_pc_reached = cpu_result.stop_pc_reached;

        if (cpu_result.memory_fault) {
            return failure(result, A32ServiceDispatchError::MemoryFault);
        }

        if (cpu_result.svc_immediate.has_value()) {
            const std::uint32_t immediate = *cpu_result.svc_immediate;
            if (result.services_handled >= max_service_calls) {
                return failure(result,
                               A32ServiceDispatchError::ServiceLimitExceeded,
                               immediate);
            }

            const A32HostServiceDisposition disposition =
                handler.handle(memory, immediate, result.regs, result.cpsr);
            if (disposition == A32HostServiceDisposition::Unhandled) {
                return failure(result,
                               A32ServiceDispatchError::ServiceUnhandled,
                               immediate);
            }
            if (disposition == A32HostServiceDisposition::Failed) {
                return failure(result,
                               A32ServiceDispatchError::ServiceFailed,
                               immediate);
            }

            ++result.services_handled;
            if (disposition == A32HostServiceDisposition::Suspended) {
                result.service_suspended = true;
                result.suspended_svc_immediate = immediate;
                return result;
            }

            if (stop_pc.has_value() && result.regs[15] == *stop_pc) {
                result.stop_pc_reached = true;
                return result;
            }
            if (remaining_instructions == 0) {
                if (stop_pc.has_value()) {
                    return failure(
                        result,
                        A32ServiceDispatchError::InstructionLimitExceeded);
                }
                return result;
            }

            request.regs = result.regs;
            request.entry_pc = result.regs[15];
            request.initial_cpsr = result.cpsr;
            request.stop_pc = stop_pc;
            continue;
        }

        if (cpu_result.exception_raised) {
            return failure(result, A32ServiceDispatchError::CpuException);
        }
        if (cpu_result.stop_pc_reached) {
            return result;
        }

        // Without a trap/fault/stop, cpu::execute consumed the requested
        // bounded slice. A caller that requested a stop PC did not reach it.
        if (stop_pc.has_value()) {
            return failure(result,
                           A32ServiceDispatchError::InstructionLimitExceeded);
        }
        return result;
    }
}

A32ServiceDispatchResult execute_a32_with_services(
    memory::GuestMemory& memory,
    cpu::ExecutionRequest request,
    A32HostServiceHandler& handler,
    std::size_t max_service_calls) {
    cpu::A32Executor executor{memory};
    return execute_a32_with_services(
        executor, request, handler, max_service_calls);
}

std::optional<cpu::ExecutionRequest> make_a32_service_resume_request(
    const A32ServiceDispatchResult& suspended_result,
    std::size_t instruction_budget,
    std::optional<std::uint32_t> stop_pc) {
    if (!suspended_result.service_suspended ||
        !suspended_result.suspended_svc_immediate.has_value() ||
        instruction_budget == 0U) {
        return std::nullopt;
    }

    cpu::ExecutionRequest request{};
    request.instruction_set =
        (suspended_result.cpsr & 0x20U) != 0U
            ? cpu::InstructionSet::Thumb
            : cpu::InstructionSet::Arm;
    request.entry_pc = suspended_result.regs[15];
    request.regs = suspended_result.regs;
    request.instruction_count = instruction_budget;
    request.stop_pc = stop_pc;
    request.initial_cpsr = suspended_result.cpsr;
    return request;
}

const char* to_string(A32ServiceDispatchError error) noexcept {
    switch (error) {
    case A32ServiceDispatchError::None:
        return "none";
    case A32ServiceDispatchError::MemoryFault:
        return "memory_fault";
    case A32ServiceDispatchError::CpuException:
        return "cpu_exception";
    case A32ServiceDispatchError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32ServiceDispatchError::ServiceUnhandled:
        return "service_unhandled";
    case A32ServiceDispatchError::ServiceFailed:
        return "service_failed";
    case A32ServiceDispatchError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::runtime
