#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceHandler;
using liba32android::runtime::A32ServiceDispatchError;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::execute_a32_with_services;
using liba32android::runtime::make_a32_service_resume_request;

constexpr std::size_t kMemorySize = 4096;
constexpr std::uint32_t kStopPc = static_cast<std::uint32_t>(kMemorySize);

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u32(LinearGuestMemory& memory,
               std::uint32_t address,
               std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
    return memory.write(address, bytes);
}

bool read_u32(const LinearGuestMemory& memory,
              std::uint32_t address,
              std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

class RecordingHandler final : public A32HostServiceHandler {
public:
    A32HostServiceDisposition disposition{A32HostServiceDisposition::Handled};
    std::uint32_t replacement_r0{40};
    bool write_marker{};
    std::vector<std::uint32_t> calls;

    A32HostServiceDisposition handle(
        liba32android::memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t&) override {
        calls.push_back(svc_immediate);
        regs[0] = replacement_r0;
        if (write_marker) {
            const std::array<std::uint8_t, 4> marker{
                0x44, 0x33, 0x22, 0x11,
            };
            if (!memory.write(0x300, marker)) {
                return A32HostServiceDisposition::Failed;
            }
        }
        return disposition;
    }
};

int test_arm_and_thumb_service_resume() {
    {
        // mov r0,#1; svc #0x42; add r0,r0,#2; bx lr
        constexpr std::array<std::uint8_t, 16> code{
            0x01, 0x00, 0xA0, 0xE3,
            0x42, 0x00, 0x00, 0xEF,
            0x02, 0x00, 0x80, 0xE2,
            0x1E, 0xFF, 0x2F, 0xE1,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage ARM service code");

        RecordingHandler handler;
        handler.write_marker = true;
        ExecutionRequest request{};
        request.regs[14] = kStopPc;
        request.instruction_count = 4;
        request.stop_pc = kStopPc;

        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        std::uint32_t marker = 0;
        if (!read_u32(memory, 0x300, marker) ||
            !result || result.instructions_executed != 4 ||
            result.services_handled != 1 || !result.stop_pc_reached ||
            result.regs[0] != 42 ||
            handler.calls != std::vector<std::uint32_t>{0x42U} ||
            marker != 0x11223344U) {
            return fail("ARM host-service dispatch/resume failed");
        }
    }

    {
        // movs r0,#1; svc #0x7a; adds r0,#2; bx lr
        constexpr std::array<std::uint8_t, 8> code{
            0x01, 0x20,
            0x7A, 0xDF,
            0x02, 0x30,
            0x70, 0x47,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage Thumb service code");

        RecordingHandler handler;
        ExecutionRequest request{};
        request.instruction_set = InstructionSet::Thumb;
        request.regs[14] = kStopPc | 1U;
        request.instruction_count = 4;
        request.stop_pc = kStopPc;

        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (!result || result.instructions_executed != 4 ||
            result.services_handled != 1 || !result.stop_pc_reached ||
            result.regs[0] != 42 ||
            (result.cpsr & 0x20U) == 0 ||
            handler.calls != std::vector<std::uint32_t>{0x7aU}) {
            return fail("Thumb host-service dispatch/resume failed");
        }
    }
    return 0;
}

int test_service_limit_and_completed_side_effects() {
    // svc #1; svc #2; bx lr
    constexpr std::array<std::uint8_t, 12> code{
        0x01, 0x00, 0x00, 0xEF,
        0x02, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    LinearGuestMemory memory{kMemorySize};
    if (!memory.write(0, code)) return fail("could not stage service-limit code");

    RecordingHandler handler;
    handler.write_marker = true;
    ExecutionRequest request{};
    request.regs[14] = kStopPc;
    request.instruction_count = 3;
    request.stop_pc = kStopPc;

    const auto result =
        execute_a32_with_services(memory, request, handler, 1);
    std::uint32_t marker = 0;
    if (!read_u32(memory, 0x300, marker) ||
        result.error != A32ServiceDispatchError::ServiceLimitExceeded ||
        result.instructions_executed != 2 ||
        result.services_handled != 1 ||
        !result.failing_svc_immediate.has_value() ||
        *result.failing_svc_immediate != 2U ||
        handler.calls != std::vector<std::uint32_t>{1U} ||
        marker != 0x11223344U) {
        return fail("service ceiling did not preserve completed handler effects");
    }
    return 0;
}

int test_unhandled_failed_and_final_budget_service() {
    constexpr std::array<std::uint8_t, 4> code{
        0x07, 0x00, 0x00, 0xEF,
    };

    {
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage unhandled service");
        RecordingHandler handler;
        handler.disposition = A32HostServiceDisposition::Unhandled;
        ExecutionRequest request{};
        request.instruction_count = 1;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (result.error != A32ServiceDispatchError::ServiceUnhandled ||
            !result.failing_svc_immediate.has_value() ||
            *result.failing_svc_immediate != 7U ||
            result.services_handled != 0) {
            return fail("unhandled service was not surfaced distinctly");
        }
    }

    {
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage failed service");
        RecordingHandler handler;
        handler.disposition = A32HostServiceDisposition::Failed;
        ExecutionRequest request{};
        request.instruction_count = 1;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (result.error != A32ServiceDispatchError::ServiceFailed ||
            !result.failing_svc_immediate.has_value() ||
            *result.failing_svc_immediate != 7U ||
            result.services_handled != 0) {
            return fail("failed service was not surfaced distinctly");
        }
    }

    {
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage final-budget service");
        RecordingHandler handler;
        ExecutionRequest request{};
        request.instruction_count = 1;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (!result || result.instructions_executed != 1 ||
            result.services_handled != 1 ||
            handler.calls != std::vector<std::uint32_t>{7U}) {
            return fail("final-budget handled service did not complete without stop target");
        }
    }
    return 0;
}

int test_cpu_failures_and_budget_compatibility() {
    {
        LinearGuestMemory memory{kMemorySize};
        RecordingHandler handler;
        ExecutionRequest request{};
        request.entry_pc = static_cast<std::uint32_t>(kMemorySize - 2);
        request.instruction_count = 1;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (result.error != A32ServiceDispatchError::MemoryFault) {
            return fail("dispatcher did not propagate CPU memory fault");
        }
    }

    {
        // udf #0
        constexpr std::array<std::uint8_t, 4> code{
            0xF0, 0x00, 0xF0, 0xE7,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) return fail("could not stage UDF code");
        RecordingHandler handler;
        ExecutionRequest request{};
        request.instruction_count = 1;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (result.error != A32ServiceDispatchError::CpuException ||
            result.failing_svc_immediate.has_value()) {
            return fail("dispatcher did not classify non-SVC CPU exception");
        }
    }

    constexpr std::array<std::uint8_t, 4> loop_code{
        0xFE, 0xFF, 0xFF, 0xEA,
    };
    {
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, loop_code)) return fail("could not stage budget loop");
        RecordingHandler handler;
        ExecutionRequest request{};
        request.instruction_count = 2;
        request.stop_pc = kStopPc;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (result.error != A32ServiceDispatchError::InstructionLimitExceeded ||
            result.instructions_executed != 2 ||
            result.stop_pc_reached) {
            return fail("dispatcher did not enforce total budget before stop PC");
        }
    }

    {
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, loop_code)) return fail("could not stage compatibility loop");
        RecordingHandler handler;
        ExecutionRequest request{};
        request.instruction_count = 2;
        const auto result =
            execute_a32_with_services(memory, request, handler, 1);
        if (!result || result.instructions_executed != 2 ||
            result.services_handled != 0 || result.stop_pc_reached) {
            return fail("no-stop dispatcher changed fixed-budget completion");
        }
    }
    return 0;
}


int test_service_suspension_and_resume() {
    {
        // svc #0x44; add r0,r0,#2; bx lr
        constexpr std::array<std::uint8_t, 12> code{
            0x44, 0x00, 0x00, 0xEF,
            0x02, 0x00, 0x80, 0xE2,
            0x1E, 0xFF, 0x2F, 0xE1,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) {
            return fail("could not stage ARM suspension program");
        }

        RecordingHandler handler;
        handler.disposition = A32HostServiceDisposition::Suspended;
        handler.replacement_r0 = 40U;

        ExecutionRequest request{};
        request.regs[14] = kStopPc;
        request.instruction_count = 3;
        request.stop_pc = kStopPc;

        const auto suspended =
            execute_a32_with_services(memory, request, handler, 1);
        if (!suspended || !suspended.service_suspended ||
            !suspended.suspended_svc_immediate.has_value() ||
            *suspended.suspended_svc_immediate != 0x44U ||
            suspended.failing_svc_immediate.has_value() ||
            suspended.services_handled != 1 ||
            suspended.instructions_executed != 1 ||
            suspended.stop_pc_reached ||
            suspended.regs[0] != 40U ||
            handler.calls != std::vector<std::uint32_t>{0x44U}) {
            return fail("ARM service suspension snapshot was incorrect");
        }

        const auto resumed_request =
            make_a32_service_resume_request(suspended, 2, kStopPc);
        if (!resumed_request.has_value() ||
            resumed_request->entry_pc != suspended.regs[15] ||
            resumed_request->regs != suspended.regs ||
            !resumed_request->initial_cpsr.has_value() ||
            *resumed_request->initial_cpsr != suspended.cpsr ||
            resumed_request->instruction_set != InstructionSet::Arm) {
            return fail("ARM suspended service did not produce exact resume request");
        }

        RecordingHandler resumed_handler;
        const auto resumed = execute_a32_with_services(
            memory, *resumed_request, resumed_handler, 1);
        if (!resumed || resumed.service_suspended ||
            !resumed.stop_pc_reached ||
            resumed.instructions_executed != 2 ||
            resumed.regs[0] != 42U ||
            !resumed_handler.calls.empty()) {
            return fail("ARM suspended service did not resume after SVC");
        }
    }

    {
        // svc #0x55; adds r0,#1; bx lr
        constexpr std::array<std::uint8_t, 6> code{
            0x55, 0xDF,
            0x01, 0x30,
            0x70, 0x47,
        };
        LinearGuestMemory memory{kMemorySize};
        if (!memory.write(0, code)) {
            return fail("could not stage Thumb suspension program");
        }

        RecordingHandler handler;
        handler.disposition = A32HostServiceDisposition::Suspended;
        handler.replacement_r0 = 9U;

        ExecutionRequest request{};
        request.instruction_set = InstructionSet::Thumb;
        request.regs[14] = kStopPc | 1U;
        request.instruction_count = 3;
        request.stop_pc = kStopPc;

        const auto suspended =
            execute_a32_with_services(memory, request, handler, 1);
        const auto resumed_request =
            make_a32_service_resume_request(suspended, 2, kStopPc);
        if (!suspended || !suspended.service_suspended ||
            !resumed_request.has_value() ||
            resumed_request->instruction_set != InstructionSet::Thumb ||
            !resumed_request->initial_cpsr.has_value() ||
            (*resumed_request->initial_cpsr & 0x20U) == 0U) {
            return fail("Thumb service suspension lost execution state");
        }

        RecordingHandler resumed_handler;
        const auto resumed = execute_a32_with_services(
            memory, *resumed_request, resumed_handler, 1);
        if (!resumed || !resumed.stop_pc_reached ||
            resumed.regs[0] != 10U ||
            !resumed_handler.calls.empty()) {
            return fail("Thumb suspended service did not resume after SVC");
        }
    }

    A32ServiceDispatchResult ordinary{};
    if (make_a32_service_resume_request(ordinary, 1).has_value()) {
        return fail("non-suspended result unexpectedly produced continuation");
    }
    ordinary.service_suspended = true;
    ordinary.suspended_svc_immediate = 1U;
    if (make_a32_service_resume_request(ordinary, 0).has_value()) {
        return fail("zero-budget suspended result unexpectedly produced continuation");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_arm_and_thumb_service_resume(); status != 0) {
        return status;
    }
    if (const int status = test_service_limit_and_completed_side_effects();
        status != 0) {
        return status;
    }
    if (const int status = test_unhandled_failed_and_final_budget_service();
        status != 0) {
        return status;
    }
    if (const int status = test_cpu_failures_and_budget_compatibility();
        status != 0) {
        return status;
    }
    if (const int status = test_service_suspension_and_resume();
        status != 0) {
        return status;
    }
    return 0;
}
