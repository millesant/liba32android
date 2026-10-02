#include <cstdint>
#include <iostream>

#include "cpu/a32_cpu.h"
#include "runtime/a32_logical_thread.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::runtime::A32LogicalThreadId;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::make_a32_logical_execution_context;
using liba32android::runtime::make_a32_service_resume_context;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

int test_logical_thread_ids_and_contexts() {
    if (A32LogicalThreadId::from_raw(0U).has_value()) {
        return fail("zero logical thread ID was accepted");
    }

    const auto thread_one = A32LogicalThreadId::from_raw(1U);
    const auto thread_two = A32LogicalThreadId::from_raw(2U);
    if (!thread_one.has_value() || !thread_two.has_value() ||
        *thread_one == *thread_two ||
        thread_one->value() != 1U ||
        thread_two->value() != 2U) {
        return fail("logical thread IDs were not distinct");
    }

    ExecutionRequest first{};
    first.entry_pc = 0x100U;
    first.regs[0] = 0x11111111U;
    first.instruction_count = 7U;

    ExecutionRequest second{};
    second.instruction_set = InstructionSet::Thumb;
    second.entry_pc = 0x201U;
    second.regs[0] = 0x22222222U;
    second.instruction_count = 5U;

    const auto first_context =
        make_a32_logical_execution_context(*thread_one, first);
    const auto second_context =
        make_a32_logical_execution_context(*thread_two, second);
    if (!first_context.has_value() || !second_context.has_value() ||
        !first_context->valid() || !second_context->valid() ||
        first_context->thread_id != *thread_one ||
        second_context->thread_id != *thread_two ||
        first_context->request.entry_pc != 0x100U ||
        second_context->request.entry_pc != 0x201U) {
        return fail("logical execution contexts lost identity or request state");
    }

    ExecutionRequest zero_budget{};
    zero_budget.instruction_count = 0U;
    if (make_a32_logical_execution_context(
            *thread_one, zero_budget).has_value()) {
        return fail("zero-budget logical execution context was accepted");
    }

    if (make_a32_logical_execution_context(
            A32LogicalThreadId{}, first).has_value()) {
        return fail("invalid logical thread identity was accepted");
    }

    return 0;
}

int test_suspended_resume_context() {
    const auto thread = A32LogicalThreadId::from_raw(9U);
    if (!thread.has_value()) {
        return fail("could not create logical thread ID");
    }

    A32ServiceDispatchResult suspended{};
    suspended.service_suspended = true;
    suspended.suspended_svc_immediate = 0x44U;
    suspended.regs[0] = 0xA5A5A5A5U;
    suspended.regs[15] = 0x204U;
    suspended.cpsr = 0x20U;

    const auto resumed = make_a32_service_resume_context(
        *thread,
        suspended,
        3U,
        0x300U);
    if (!resumed.has_value() ||
        resumed->thread_id != *thread ||
        resumed->request.entry_pc != 0x204U ||
        resumed->request.regs != suspended.regs ||
        !resumed->request.initial_cpsr.has_value() ||
        *resumed->request.initial_cpsr != suspended.cpsr ||
        resumed->request.instruction_set != InstructionSet::Thumb ||
        resumed->request.instruction_count != 3U ||
        !resumed->request.stop_pc.has_value() ||
        *resumed->request.stop_pc != 0x300U) {
        return fail("suspended result did not preserve logical continuation");
    }

    A32ServiceDispatchResult ordinary{};
    if (make_a32_service_resume_context(
            *thread, ordinary, 1U).has_value()) {
        return fail("ordinary result unexpectedly produced logical continuation");
    }
    if (make_a32_service_resume_context(
            *thread, suspended, 0U).has_value()) {
        return fail("zero-budget suspended result produced logical continuation");
    }
    if (make_a32_service_resume_context(
            A32LogicalThreadId{}, suspended, 1U).has_value()) {
        return fail("invalid logical identity produced continuation");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int status = test_logical_thread_ids_and_contexts();
        status != 0) {
        return status;
    }
    if (const int status = test_suspended_resume_context(); status != 0) {
        return status;
    }
    return 0;
}
