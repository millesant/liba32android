#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>

#include "compat/a32_aeabi_atexit.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_lifecycle.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiObjectDsoBinding;
using liba32android::compat::A32AeabiAtexitRecordStatus;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32AeabiFinalizeError;
using liba32android::compat::A32AeabiFinalizeOptions;
using liba32android::compat::A32CxaFinalizeService;
using liba32android::compat::kA32AeabiAtexitSvcImmediate;
using liba32android::compat::kA32CxaFinalizeSvcImmediate;
using liba32android::cpu::ExecutionRequest;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::execute_a32_with_services;
using liba32android::elf::Elf32InitCall;
using liba32android::elf::Elf32InitExecutionOptions;
using liba32android::elf::Elf32LifecycleExecutionContext;
using liba32android::elf::execute_elf32_init_calls;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

int test_exact_registration_and_capacity() {
    LinearGuestMemory memory{64};
    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService service{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    regs[0] = 0x11111111U;
    regs[1] = 0x22222221U;
    regs[2] = 0x33333333U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 1U ||
        service.records()[0].object != 0x11111111U ||
        service.records()[0].destructor != 0x22222221U ||
        service.records()[0].dso_handle != 0x33333333U) {
        return fail("__aeabi_atexit did not preserve exact guest registration");
    }

    regs = {};
    regs[0] = 0x44444444U;
    regs[1] = 0x55555554U;
    regs[2] = 0x66666666U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 2U) {
        return fail("second __aeabi_atexit registration failed");
    }

    regs = {};
    regs[0] = 0x77777777U;
    regs[1] = 0x88888888U;
    regs[2] = 0x99999999U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.records()[1].object != 0x44444444U) {
        return fail("__aeabi_atexit capacity failure mutated registrations");
    }

    regs = {};
    if (service.handle(memory, 0xD4U, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown __aeabi_atexit service ID was not unhandled");
    }
    return 0;
}

std::uint32_t read_u32_le(
    const LinearGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return 0xffffffffU;
    }
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

int test_reverse_per_dso_finalization_and_once_state() {
    LinearGuestMemory memory{4096};
    constexpr std::array<std::uint8_t, 16> store_object{
        0x04, 0x10, 0x9F, 0xE5,
        0x00, 0x00, 0x81, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x03, 0x00, 0x00,
    };
    if (!memory.write(0x100U, store_object)) {
        return fail("could not stage registered destructor code");
    }

    std::array<A32AeabiAtexitRecord, 4> records{};
    A32AeabiAtexitService service{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    const auto register_one =
        [&](std::uint32_t object, std::uint32_t dso) {
            regs = {};
            regs[0] = object;
            regs[1] = 0x100U;
            regs[2] = dso;
            return service.handle(
                memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) ==
                    A32HostServiceDisposition::Handled &&
                regs[0] == 0U;
        };

    if (!register_one(0x11111111U, 0xAAAA0000U) ||
        !register_one(0x22222222U, 0xAAAA0000U) ||
        !register_one(0x33333333U, 0xBBBB0000U)) {
        return fail("could not register finalization fixture");
    }

    const A32AeabiFinalizeOptions one_callback{
        .stack_top = 0x0ff8U,
        .return_pc = 0x1000U,
        .max_instructions_per_call = 16U,
        .max_callbacks = 1U,
    };
    const auto limited =
        service.finalize(memory, 0xAAAA0000U, one_callback);
    if (limited.error != A32AeabiFinalizeError::CallbackLimitExceeded ||
        limited.callbacks_completed != 0U ||
        service.records()[0].status != A32AeabiAtexitRecordStatus::Pending ||
        service.records()[1].status != A32AeabiAtexitRecordStatus::Pending ||
        read_u32_le(memory, 0x300U) != 0U) {
        return fail("callback ceiling did not fail before guest side effects");
    }

    const A32AeabiFinalizeOptions options{
        .stack_top = 0x0ff8U,
        .return_pc = 0x1000U,
        .max_instructions_per_call = 16U,
        .max_callbacks = 4U,
    };
    const auto dso_result =
        service.finalize(memory, 0xAAAA0000U, options);
    if (!dso_result ||
        dso_result.callbacks_completed != 2U ||
        read_u32_le(memory, 0x300U) != 0x11111111U ||
        service.records()[0].status != A32AeabiAtexitRecordStatus::Complete ||
        service.records()[1].status != A32AeabiAtexitRecordStatus::Complete ||
        service.records()[2].status != A32AeabiAtexitRecordStatus::Pending) {
        return fail("per-DSO registered destructors did not execute in reverse order");
    }

    const auto all_result =
        service.finalize(memory, std::nullopt, options);
    if (!all_result ||
        all_result.callbacks_completed != 1U ||
        read_u32_le(memory, 0x300U) != 0x33333333U ||
        service.records()[2].status != A32AeabiAtexitRecordStatus::Complete) {
        return fail("process-wide finalization did not execute remaining record");
    }

    const auto repeated =
        service.finalize(memory, std::nullopt, options);
    if (!repeated || repeated.callbacks_completed != 0U) {
        return fail("completed registered destructors were replayed");
    }
    return 0;
}

int test_finalization_failure_latches_record() {
    LinearGuestMemory memory{4096};
    constexpr std::array<std::uint8_t, 4> loop_code{
        0xFE, 0xFF, 0xFF, 0xEA,
    };
    if (!memory.write(0x100U, loop_code)) {
        return fail("could not stage failing registered destructor");
    }

    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService service{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x11111111U;
    regs[1] = 0x100U;
    regs[2] = 0xAAAA0000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not register failing destructor");
    }

    const A32AeabiFinalizeOptions options{
        .stack_top = 0x0ff8U,
        .return_pc = 0x1000U,
        .max_instructions_per_call = 2U,
        .max_callbacks = 1U,
    };
    const auto first =
        service.finalize(memory, 0xAAAA0000U, options);
    if (first.error != A32AeabiFinalizeError::InstructionLimitExceeded ||
        first.callbacks_completed != 0U ||
        first.failing_record != 0U ||
        service.records()[0].status != A32AeabiAtexitRecordStatus::Failed) {
        return fail("failed registered destructor was not latched");
    }

    const auto repeated =
        service.finalize(memory, 0xAAAA0000U, options);
    if (repeated.error != A32AeabiFinalizeError::InvalidRecordState ||
        repeated.callbacks_completed != 0U ||
        repeated.failing_record != 0U) {
        return fail("failed registered destructor was replayed");
    }
    return 0;
}

int test_guest_cxa_finalize_service() {
    constexpr std::array<std::uint8_t, 8> finalize_code{
        0xD3, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 16> destructor_code{
        0x04, 0x10, 0x9F, 0xE5,
        0x00, 0x00, 0x81, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x03, 0x00, 0x00,
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0U, finalize_code) ||
        !memory.write(0x100U, destructor_code)) {
        return fail("could not stage guest __cxa_finalize fixture");
    }

    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0xAABBCCDDU;
    regs[1] = 0x100U;
    regs[2] = 0x77770000U;
    if (registrations.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not register guest __cxa_finalize fixture");
    }

    A32CxaFinalizeService finalizer{
        registrations,
        A32AeabiFinalizeOptions{
            // Guest service execution must ignore this configured stack top
            // and use the trapped caller's live r13 instead.
            .stack_top = 0U,
            .return_pc = 0x1000U,
            .max_instructions_per_call = 16U,
            .max_callbacks = 2U,
        },
    };
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32CxaFinalizeSvcImmediate, &finalizer},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 0x77770000U;
    request.regs[13] = 0x0ff8U;
    request.regs[14] = 4096U;
    request.instruction_count = 2U;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1U);
    if (!result ||
        !result.stop_pc_reached ||
        result.services_handled != 1U ||
        read_u32_le(memory, 0x300U) != 0xAABBCCDDU ||
        registrations.records()[0].status !=
            A32AeabiAtexitRecordStatus::Complete ||
        !finalizer.last_result().has_value() ||
        !*finalizer.last_result() ||
        finalizer.last_result()->callbacks_completed != 1U) {
        return fail("guest __cxa_finalize SVC did not run matching destructor");
    }
    return 0;
}

int test_lifecycle_context_learns_dso_binding() {
    constexpr std::array<std::uint8_t, 32> code{
        0x0C, 0x00, 0x9F, 0xE5,  // ldr r0, [pc, #12]
        0x0C, 0x10, 0x9F, 0xE5,  // ldr r1, [pc, #12]
        0x0C, 0x20, 0x9F, 0xE5,  // ldr r2, [pc, #12]
        0xD2, 0x00, 0x00, 0xEF,  // svc #0xd2
        0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
        0x11, 0x11, 0x11, 0x11,  // object
        0x21, 0x22, 0x22, 0x22,  // destructor
        0x00, 0x00, 0x33, 0x33,  // DSO
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0x100U, code)) {
        return fail("could not stage lifecycle-aware __aeabi_atexit fixture");
    }

    std::array<A32AeabiAtexitRecord, 4> records{};
    std::array<A32AeabiObjectDsoBinding, 1> bindings{};
    Elf32LifecycleExecutionContext context{
        .object_index = 9U,
    };
    A32AeabiAtexitService service{
        std::span{records},
        std::span{bindings},
        &context,
    };
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32AeabiAtexitSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};
    const std::array<Elf32InitCall, 1> calls{{
        {
            .object_index = 3U,
            .array_index = 0U,
            .function = 0x100U,
        },
    }};

    const auto executed = execute_elf32_init_calls(
        memory,
        calls,
        Elf32InitExecutionOptions{
            .stack_top = 0x0ff8U,
            .return_pc = 0x1000U,
            .max_instructions_per_call = 8U,
            .service_handler = &registry,
            .max_service_calls_per_call = 1U,
            .execution_context = &context,
        });
    bool ambiguous = false;
    const auto learned = service.dso_for_object(3U, ambiguous);
    if (!executed ||
        executed.calls_completed != 1U ||
        context.object_index != 9U ||
        service.record_count() != 1U ||
        service.binding_count() != 1U ||
        ambiguous ||
        learned != 0x33330000U ||
        service.learned_bindings()[0].object_index != 3U ||
        service.learned_bindings()[0].dso_handle != 0x33330000U) {
        return fail("lifecycle __aeabi_atexit did not learn exact DSO binding");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    context.object_index = 3U;
    regs[0] = 0x44444444U;
    regs[1] = 0x55555555U;
    regs[2] = 0x33330000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 2U ||
        service.binding_count() != 1U) {
        return fail("identical learned DSO binding was not reused");
    }

    regs = {};
    regs[0] = 0x66666666U;
    regs[1] = 0x77777777U;
    regs[2] = 0x44440000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.binding_count() != 1U) {
        return fail("same object conflicting DSO mutated registration state");
    }

    context.object_index = 4U;
    regs = {};
    regs[0] = 0x88888888U;
    regs[1] = 0x99999999U;
    regs[2] = 0x33330000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.binding_count() != 1U) {
        return fail("same DSO conflicting object mutated registration state");
    }

    context.object_index = 5U;
    regs = {};
    regs[0] = 0xAAAA0001U;
    regs[1] = 0xBBBB0001U;
    regs[2] = 0x55550000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.binding_count() != 1U) {
        return fail("learned DSO binding capacity failure was not atomic");
    }

    records[0].status = A32AeabiAtexitRecordStatus::Complete;
    records[1].status = A32AeabiAtexitRecordStatus::Complete;
    service.forget_binding_for_object(3U);
    if (service.binding_count() != 0U) {
        return fail("retired object DSO binding was not forgotten");
    }

    context.object_index = 5U;
    regs = {};
    regs[0] = 0xAAAA0002U;
    regs[1] = 0xBBBB0002U;
    regs[2] = 0x55550000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 3U ||
        service.binding_count() != 1U ||
        service.learned_bindings()[0].object_index != 5U ||
        service.learned_bindings()[0].dso_handle != 0x55550000U) {
        return fail("forgotten DSO binding capacity was not reusable");
    }

    context.object_index.reset();
    regs = {};
    regs[0] = 0xCCCC0001U;
    regs[1] = 0xDDDD0001U;
    regs[2] = 0x66660000U;
    if (service.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        service.record_count() != 4U ||
        service.binding_count() != 1U) {
        return fail("context-free legacy registration unexpectedly learned binding");
    }
    return 0;
}

int test_cxa_atexit_guest_argument_reorder() {
    constexpr std::array<std::uint8_t, 20> code{
        0x00, 0x30, 0xA0, 0xE1,  // mov r3, r0
        0x01, 0x00, 0xA0, 0xE1,  // mov r0, r1
        0x03, 0x10, 0xA0, 0xE1,  // mov r1, r3
        0xD2, 0x00, 0x00, 0xEF,  // svc #0xd2
        0x1E, 0xFF, 0x2F, 0xE1,  // bx lr
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0U, code)) {
        return fail("could not stage __cxa_atexit ARM fixture");
    }

    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService service{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32AeabiAtexitSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.instruction_set = liba32android::cpu::InstructionSet::Arm;
    request.regs[0] = 0x22220001U;
    request.regs[1] = 0x11110000U;
    request.regs[2] = 0x33330000U;
    request.regs[14] = 4096U;
    request.instruction_count = 5U;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1U);
    if (!result ||
        !result.stop_pc_reached ||
        result.services_handled != 1U ||
        result.regs[0] != 0U ||
        service.record_count() != 1U ||
        service.records()[0].object != 0x11110000U ||
        service.records()[0].destructor != 0x22220001U ||
        service.records()[0].dso_handle != 0x33330000U) {
        return fail("__cxa_atexit did not reorder destructor/object arguments");
    }

    std::array<std::uint32_t, 16> aeabi_regs{};
    std::uint32_t cpsr{};
    aeabi_regs[0] = 0x44440000U;
    aeabi_regs[1] = 0x55550001U;
    aeabi_regs[2] = 0x66660000U;
    if (service.handle(
            memory,
            kA32AeabiAtexitSvcImmediate,
            aeabi_regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        aeabi_regs[0] != 0U ||
        service.record_count() != 2U ||
        service.records()[1].object != 0x44440000U ||
        service.records()[1].destructor != 0x55550001U ||
        service.records()[1].dso_handle != 0x66660000U) {
        return fail("__cxa_atexit did not share ordered registration state");
    }

    request.regs[0] = 0x77770001U;
    request.regs[1] = 0x88880000U;
    request.regs[2] = 0x99990000U;
    const auto exhausted =
        execute_a32_with_services(memory, request, registry, 1U);
    if (!exhausted ||
        exhausted.regs[0] != 0xffffffffU ||
        service.record_count() != 2U ||
        service.records()[0].object != 0x11110000U ||
        service.records()[1].object != 0x44440000U) {
        return fail("__cxa_atexit did not share registration capacity");
    }
    return 0;
}

int test_arm_registry_integration() {
    constexpr std::array<std::uint8_t, 8> code{
        0xD2, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };

    LinearGuestMemory memory{4096};
    if (!memory.write(0U, code)) {
        return fail("could not stage __aeabi_atexit ARM fixture");
    }

    std::array<A32AeabiAtexitRecord, 2> records{};
    A32AeabiAtexitService service{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {kA32AeabiAtexitSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    ExecutionRequest request{};
    request.regs[0] = 0x11110000U;
    request.regs[1] = 0x22220001U;
    request.regs[2] = 0x33330000U;
    request.regs[14] = 4096U;
    request.instruction_count = 2U;
    request.stop_pc = 4096U;

    const auto result =
        execute_a32_with_services(memory, request, registry, 1U);
    if (!result ||
        !result.stop_pc_reached ||
        result.services_handled != 1U ||
        result.regs[0] != 0U ||
        service.record_count() != 1U ||
        service.records()[0].object != 0x11110000U ||
        service.records()[0].destructor != 0x22220001U ||
        service.records()[0].dso_handle != 0x33330000U) {
        return fail("real ARM __aeabi_atexit SVC registration failed");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_registration_and_capacity();
        status != 0) {
        return status;
    }
    if (const int status = test_cxa_atexit_guest_argument_reorder();
        status != 0) {
        return status;
    }
    if (const int status = test_reverse_per_dso_finalization_and_once_state();
        status != 0) {
        return status;
    }
    if (const int status = test_finalization_failure_latches_record();
        status != 0) {
        return status;
    }
    if (const int status = test_guest_cxa_finalize_service();
        status != 0) {
        return status;
    }
    if (const int status = test_lifecycle_context_learns_dso_binding();
        status != 0) {
        return status;
    }
    return test_arm_registry_integration();
}
