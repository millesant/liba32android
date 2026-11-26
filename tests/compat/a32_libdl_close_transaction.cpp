#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <utility>

#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libdl_close_transaction.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiObjectDsoBinding;
using liba32android::compat::A32AeabiAtexitRecordStatus;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32AeabiFinalizeOptions;
using liba32android::compat::A32CxaFinalizeService;
using liba32android::compat::A32LibDlCloseTransaction;
using liba32android::compat::A32LibDlCloseTransactionError;
using liba32android::compat::A32LibDlCloseTransactionOptions;
using liba32android::compat::A32LibDlCloseTransactionOutcome;
using liba32android::compat::A32LibDlHandle;
using liba32android::compat::A32LibDlObjectLifecycleBinding;
using liba32android::compat::kA32AeabiAtexitSvcImmediate;
using liba32android::compat::kA32CxaFinalizeSvcImmediate;
using liba32android::elf::Elf32FunctionArrayMetadata;
using liba32android::elf::Elf32LifecycleExecutionContext;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapRootPolicy;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::uint32_t read_u32(
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

int test_last_reference_runs_exact_object_teardown() {
    LinearGuestMemory memory{0x2000};

    constexpr std::uint32_t dso = 0x44440000U;
    constexpr std::array<std::uint8_t, 16> fini_array_code{
        0x04,0x00,0x9F,0xE5,  // ldr r0, [pc, #4]
        0xD3,0x00,0x00,0xEF,  // svc #0xd3
        0x1E,0xFF,0x2F,0xE1,  // bx lr
        0x00,0x00,0x44,0x44,  // exact DSO selector
    };
    constexpr std::array<std::uint8_t, 16> registered_destructor{
        0x04,0x10,0x9F,0xE5,  // ldr r1, [pc, #4]
        0x00,0x00,0x81,0xE5,  // str r0, [r1]
        0x1E,0xFF,0x2F,0xE1,  // bx lr
        0x00,0x03,0x00,0x00,  // marker address
    };
    constexpr std::array<std::uint8_t, 4> dt_fini{
        0x1E,0xFF,0x2F,0xE1,
    };
    constexpr std::array<std::uint8_t, 4> fini_entry{
        0x00,0x01,0x00,0x00,
    };
    if (!memory.write(0x100U, fini_array_code) ||
        !memory.write(0x140U, registered_destructor) ||
        !memory.write(0x180U, dt_fini) ||
        !memory.write(0x200U, fini_entry)) {
        return fail("could not stage dlclose lifecycle fixture");
    }

    Elf32LinkMap link_map;
    Elf32LoadedDependencyObject object;
    object.identity = "resident";
    object.linker_metadata.fini_array =
        Elf32FunctionArrayMetadata{.guest_address = 0x200U, .size = 4U};
    object.linker_metadata.fini_function = 0x180U;
    link_map.graph.objects.push_back(std::move(object));

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1);
    lifecycle.objects[0].constructors = Elf32LifecycleObjectStatus::Complete;

    std::array<A32AeabiAtexitRecord, 2> records{};
    std::array<A32AeabiObjectDsoBinding, 1> learned_bindings{};
    Elf32LifecycleExecutionContext registration_context{
        .object_index = 0U,
    };
    A32AeabiAtexitService registrations{
        std::span{records},
        std::span{learned_bindings},
        &registration_context,
    };
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0xCAFEBABEU;
    regs[1] = 0x140U;
    regs[2] = dso;
    if (registrations.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U) {
        return fail("could not register dlclose destructor");
    }

    A32CxaFinalizeService finalizer{
        registrations,
        A32AeabiFinalizeOptions{
            .stack_top = 0U,
            .return_pc = 0x1000U,
            .max_instructions_per_call = 16U,
            .max_callbacks = 2U,
        },
    };
    const std::array<A32HostServiceRegistryEntry, 1> service_entries{{
        {kA32CxaFinalizeSvcImmediate, &finalizer},
    }};
    A32HostServiceRegistry registry{std::span{service_entries}};

    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 2U},
    }};
    registration_context.object_index.reset();
    A32LibDlCloseTransaction transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span<const A32LibDlObjectLifecycleBinding>{},
        A32LibDlCloseTransactionOptions{
            .max_fini_array_entries = 4U,
            .execution = {
                .stack_top = 0x0ff8U,
                .return_pc = 0x1000U,
                .max_instructions_per_call = 32U,
                .service_handler = &registry,
                .max_service_calls_per_call = 1U,
            },
        },
    };

    const auto first = transaction.close(memory, 0x70000000U);
    if (!first ||
        first.outcome != A32LibDlCloseTransactionOutcome::RefcountDecremented ||
        handles[0].refcount != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        read_u32(memory, 0x300U) != 0U) {
        return fail("non-final dlclose did not only decrement refcount");
    }

    const auto last = transaction.close(memory, 0x70000000U);
    if (!last ||
        last.outcome != A32LibDlCloseTransactionOutcome::ObjectFinalized ||
        last.fini_calls_completed != 2U ||
        handles[0].refcount != 0U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Complete ||
        registrations.records()[0].status !=
            A32AeabiAtexitRecordStatus::Complete ||
        read_u32(memory, 0x300U) != 0xCAFEBABEU) {
        return fail("last-reference dlclose teardown ordering failed");
    }
    return 0;
}

int test_explicit_and_learned_binding_disagreement_fails() {
    LinearGuestMemory memory{0x1000U};
    Elf32LinkMap link_map;
    Elf32LoadedDependencyObject object;
    object.identity = "binding-conflict";
    link_map.graph.objects.push_back(std::move(object));

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1U);
    lifecycle.objects[0].constructors =
        Elf32LifecycleObjectStatus::Complete;

    std::array<A32AeabiAtexitRecord, 1> records{};
    std::array<A32AeabiObjectDsoBinding, 1> learned_bindings{};
    Elf32LifecycleExecutionContext context{
        .object_index = 0U,
    };
    A32AeabiAtexitService registrations{
        std::span{records},
        std::span{learned_bindings},
        &context,
    };
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x11111111U;
    regs[1] = 0x140U;
    regs[2] = 0xAAAA0000U;
    if (registrations.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        registrations.binding_count() != 1U) {
        return fail("could not stage learned DSO conflict fixture");
    }
    context.object_index.reset();

    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
    }};
    const std::array<A32LibDlObjectLifecycleBinding, 1> explicit_bindings{{
        {.object_index = 0U, .dso_handle = 0xBBBB0000U},
    }};
    const std::array<A32HostServiceRegistryEntry, 0> entries{};
    A32HostServiceRegistry registry{std::span{entries}};
    A32LibDlCloseTransaction transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{explicit_bindings},
        A32LibDlCloseTransactionOptions{
            .max_fini_array_entries = 1U,
            .execution = {
                .stack_top = 0x0ff8U,
                .return_pc = 0x1000U,
                .max_instructions_per_call = 16U,
                .service_handler = &registry,
                .max_service_calls_per_call = 1U,
            },
        },
    };

    const auto result = transaction.close(
        memory, 0x70000000U);
    if (result.error != A32LibDlCloseTransactionError::InvalidBinding ||
        handles[0].refcount != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        registrations.records()[0].status !=
            A32AeabiAtexitRecordStatus::Pending) {
        return fail("explicit/learned DSO disagreement was not rejected");
    }
    return 0;
}

int test_nonunloadable_exact_close_skips_teardown() {
    const auto run_case =
        [](Elf32LinkMapRootPolicy policy, bool nodelete) -> int {
            LinearGuestMemory memory{0x1000U};
            Elf32LinkMap link_map;
            Elf32LoadedDependencyObject object;
            object.identity = "retained-exact";
            link_map.graph.objects.push_back(std::move(object));
            link_map.roots.push_back({
                .object_index = 0U,
                .policy = policy,
                .nodelete = nodelete,
            });
            if (policy == Elf32LinkMapRootPolicy::Global) {
                link_map.global_scope_objects = {0U};
            }

            Elf32LifecycleState lifecycle;
            lifecycle.objects.resize(1U);
            lifecycle.objects[0].constructors =
                Elf32LifecycleObjectStatus::Complete;

            std::array<A32AeabiAtexitRecord, 1> records{};
            A32AeabiAtexitService registrations{std::span{records}};
            const std::array<A32HostServiceRegistryEntry, 0> entries{};
            A32HostServiceRegistry registry{std::span{entries}};
            std::array<A32LibDlHandle, 1> handles{{
                {
                    .guest_handle = 0x70000000U,
                    .object_index = 0U,
                    .refcount = 1U,
                },
            }};
            A32LibDlCloseTransaction transaction{
                link_map,
                std::span{handles},
                lifecycle,
                registrations,
                std::span<const A32LibDlObjectLifecycleBinding>{},
                A32LibDlCloseTransactionOptions{
                    .max_fini_array_entries = 1U,
                    .execution = {
                        .stack_top = 0x0ff8U,
                        .return_pc = 0x1000U,
                        .max_instructions_per_call = 16U,
                        .service_handler = &registry,
                        .max_service_calls_per_call = 1U,
                    },
                },
            };

            const auto result =
                transaction.close(memory, 0x70000000U);
            if (!result ||
                result.outcome !=
                    A32LibDlCloseTransactionOutcome::LoadPolicyRetained ||
                handles[0].refcount != 0U ||
                link_map.roots.size() != 1U ||
                lifecycle.objects[0].destructors !=
                    Elf32LifecycleObjectStatus::Pending) {
                return fail("exact close ignored non-unloadable root policy");
            }
            return 0;
        };

    if (const int status =
            run_case(Elf32LinkMapRootPolicy::Local, true);
        status != 0) {
        return status;
    }
    return run_case(Elf32LinkMapRootPolicy::Global, false);
}

int test_incomplete_registration_preserves_final_handle() {
    LinearGuestMemory memory{0x2000};
    Elf32LinkMap link_map;
    Elf32LoadedDependencyObject object;
    object.identity = "resident";
    link_map.graph.objects.push_back(std::move(object));

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1);
    lifecycle.objects[0].constructors = Elf32LifecycleObjectStatus::Complete;

    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x11111111U;
    regs[1] = 0x140U;
    regs[2] = 0x55550000U;
    if (registrations.handle(
            memory, kA32AeabiAtexitSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled) {
        return fail("could not stage incomplete registration");
    }

    const std::array<A32HostServiceRegistryEntry, 0> entries{};
    A32HostServiceRegistry registry{std::span{entries}};
    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
    }};
    const std::array<A32LibDlObjectLifecycleBinding, 1> bindings{{
        {.object_index = 0U, .dso_handle = 0x55550000U},
    }};
    A32LibDlCloseTransaction transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{bindings},
        A32LibDlCloseTransactionOptions{
            .max_fini_array_entries = 1U,
            .execution = {
                .stack_top = 0x0ff8U,
                .return_pc = 0x1000U,
                .max_instructions_per_call = 16U,
                .service_handler = &registry,
                .max_service_calls_per_call = 1U,
            },
        },
    };

    const auto result = transaction.close(memory, 0x70000000U);
    if (result.error !=
            A32LibDlCloseTransactionError::RegisteredFinalizationIncomplete ||
        handles[0].refcount != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Failed) {
        return fail("failed finalization did not preserve final handle");
    }
    return 0;
}


int test_nested_stack_override_uses_live_guest_sp() {
    LinearGuestMemory memory{0x2000};

    constexpr std::array<std::uint8_t, 16> fini_code{
        0x04,0x00,0x9F,0xE5,  // ldr r0, [pc, #4]
        0x00,0xD0,0x80,0xE5,  // str sp, [r0]
        0x1E,0xFF,0x2F,0xE1,  // bx lr
        0x00,0x03,0x00,0x00,  // marker address
    };
    constexpr std::array<std::uint8_t, 4> fini_entry{
        0x00,0x01,0x00,0x00,
    };
    if (!memory.write(0x100U, fini_code) ||
        !memory.write(0x200U, fini_entry)) {
        return fail("could not stage dlclose nested-stack fixture");
    }

    Elf32LinkMap link_map;
    Elf32LoadedDependencyObject object;
    object.identity = "resident";
    object.linker_metadata.fini_array =
        Elf32FunctionArrayMetadata{.guest_address = 0x200U, .size = 4U};
    link_map.graph.objects.push_back(std::move(object));

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1);
    lifecycle.objects[0].constructors = Elf32LifecycleObjectStatus::Complete;

    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> entries{};
    A32HostServiceRegistry registry{std::span{entries}};
    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
    }};
    const std::array<A32LibDlObjectLifecycleBinding, 1> bindings{{
        {.object_index = 0U, .dso_handle = 0x66660000U},
    }};

    A32LibDlCloseTransaction transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{bindings},
        A32LibDlCloseTransactionOptions{
            .max_fini_array_entries = 1U,
            .execution = {
                .stack_top = 0x0ff8U,
                .return_pc = 0x1000U,
                .max_instructions_per_call = 16U,
                .service_handler = &registry,
                .max_service_calls_per_call = 1U,
            },
        },
    };

    constexpr std::uint32_t live_guest_sp = 0x0df8U;
    const auto result = transaction.close(
        memory,
        0x70000000U,
        live_guest_sp);
    if (!result ||
        result.outcome != A32LibDlCloseTransactionOutcome::ObjectFinalized ||
        read_u32(memory, 0x300U) != live_guest_sp) {
        return fail("dlclose transaction did not use trapped live guest stack");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_last_reference_runs_exact_object_teardown();
        status != 0) {
        return status;
    }
    if (const int status = test_nested_stack_override_uses_live_guest_sp();
        status != 0) {
        return status;
    }
    if (const int status = test_nonunloadable_exact_close_skips_teardown();
        status != 0) {
        return status;
    }
    if (const int status =
            test_explicit_and_learned_binding_disagreement_fails();
        status != 0) {
        return status;
    }
    return test_incomplete_registration_preserves_final_handle();
}
