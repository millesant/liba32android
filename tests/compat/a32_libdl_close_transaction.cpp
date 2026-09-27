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
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
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
    A32AeabiAtexitService registrations{std::span{records}};
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
    const std::array<A32LibDlObjectLifecycleBinding, 1> bindings{{
        {.object_index = 0U, .dso_handle = dso},
    }};
    A32LibDlCloseTransaction transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{bindings},
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

}  // namespace

int main() {
    if (const int status = test_last_reference_runs_exact_object_teardown();
        status != 0) {
        return status;
    }
    return test_incomplete_registration_preserves_final_handle();
}
