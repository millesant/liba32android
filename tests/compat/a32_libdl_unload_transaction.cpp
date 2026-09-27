#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libdl.h"
#include "compat/a32_libdl_close_transaction.h"
#include "compat/a32_libdl_unload_transaction.h"
#include "elf/elf32_link_map.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32LibDlCloseTransaction;
using liba32android::compat::A32LibDlCloseTransactionError;
using liba32android::compat::A32LibDlCloseTransactionOptions;
using liba32android::compat::A32LibDlHandle;
using liba32android::compat::A32LibDlObjectLifecycleBinding;
using liba32android::compat::A32LibDlOptions;
using liba32android::compat::A32LibDlService;
using liba32android::compat::A32LibDlUnloadTransaction;
using liba32android::compat::A32LibDlUnloadTransactionError;
using liba32android::compat::A32LibDlUnloadTransactionOptions;
using liba32android::compat::A32LibDlUnloadTransactionOutcome;
using liba32android::compat::kA32LibDlDlcloseSvcImmediate;
using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapObjectState;
using liba32android::elf::Elf32LinkMapRoot;
using liba32android::elf::Elf32LinkMapRootPolicy;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::elf::Elf32LoadedSegment;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool stage_object(
    MappedGuestMemory& memory,
    Elf32LinkMap& link_map,
    const char* identity,
    std::uint32_t mapping_start) {
    const auto permissions =
        MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(mapping_start, memory.page_size(), permissions)) {
        return false;
    }

    Elf32LoadedDependencyObject object;
    object.identity = identity;
    object.load.load_bias = mapping_start;
    object.load.segments.push_back(Elf32LoadedSegment{
        .guest_address = mapping_start,
        .file_size = 4U,
        .memory_size = static_cast<std::uint32_t>(memory.page_size()),
        .mapping_start = mapping_start,
        .mapping_size = memory.page_size(),
        .permissions = permissions,
    });
    link_map.graph.objects.push_back(std::move(object));
    return true;
}

bool mapped(
    const MappedGuestMemory& memory,
    const Elf32LinkMap& link_map,
    std::size_t object_index) {
    return memory.is_mapped(
        link_map.graph.objects[object_index].load.segments[0].mapping_start);
}

A32LibDlCloseTransactionOptions close_options(
    A32HostServiceRegistry& registry) {
    A32LibDlCloseTransactionOptions options;
    options.max_fini_array_entries = 8U;
    options.execution.stack_top = 0x8ff8U;
    options.execution.return_pc = 0x9000U;
    options.execution.max_instructions_per_call = 32U;
    options.execution.service_handler = &registry;
    options.execution.max_service_calls_per_call = 1U;
    return options;
}

A32LibDlUnloadTransactionOptions unload_options() {
    A32LibDlUnloadTransactionOptions options;
    options.planning.max_objects = 8U;
    options.reclamation.max_objects = 8U;
    options.reclamation.max_segments = 16U;
    options.reclamation.max_snapshot_bytes = 1U << 20;
    return options;
}

A32LibDlOptions service_options() {
    A32LibDlOptions result;
    result.max_name_bytes = 64U;
    result.handle_base = 0x70000000U;
    result.error_buffer_address = 0x91000U;
    result.error_buffer_bytes = 128U;
    result.info_string_buffer_address = 0x92000U;
    result.info_string_buffer_bytes = 256U;
    result.symbols.max_symbols = 64U;
    result.symbols.max_hash_buckets = 64U;
    result.symbols.max_gnu_bloom_words = 32U;
    result.symbols.max_scope_objects = 8U;
    result.symbols.max_name_bytes = 64U;
    result.symbols.max_version_records = 64U;
    return result;
}

int test_service_unloads_root_but_retains_shared_dependency() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    if (!stage_object(memory, link_map, "root-a", 0x10000U) ||
        !stage_object(memory, link_map, "shared", 0x12000U) ||
        !stage_object(memory, link_map, "root-b", 0x14000U)) {
        return fail("could not map shared-root unload fixture");
    }
    link_map.graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{.requested_name = "shared.so", .target_object = 1U});
    link_map.graph.objects[2].dependencies.push_back(
        Elf32DependencyEdge{.requested_name = "shared.so", .target_object = 1U});
    link_map.roots = {
        Elf32LinkMapRoot{.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local},
        Elf32LinkMapRoot{.object_index = 2U, .policy = Elf32LinkMapRootPolicy::Local},
    };
    link_map.object_states.assign(3U, Elf32LinkMapObjectState::Active);

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(3U);
    for (auto& state : lifecycle.objects) {
        state.constructors = Elf32LifecycleObjectStatus::Complete;
    }

    std::array<A32LibDlHandle, 2> handles{};
    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> service_entries{};
    A32HostServiceRegistry registry{std::span{service_entries}};
    std::array<A32LibDlObjectLifecycleBinding, 3> bindings{{
        {.object_index = 0U, .dso_handle = 0xA000U},
        {.object_index = 1U, .dso_handle = 0xB000U},
        {.object_index = 2U, .dso_handle = 0xC000U},
    }};
    A32LibDlCloseTransaction finalizer{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span<const A32LibDlObjectLifecycleBinding>{bindings},
        close_options(registry),
    };
    A32LibDlUnloadTransaction unload{
        link_map,
        std::span{handles},
        lifecycle,
        finalizer,
        unload_options(),
    };
    A32LibDlService service{
        link_map,
        std::span{handles},
        service_options(),
        &finalizer,
        nullptr,
        &unload,
    };
    handles[0] = A32LibDlHandle{
        .guest_handle = 0x70000000U,
        .object_index = 0U,
        .refcount = 1U,
    };

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    regs[0] = 0x70000000U;
    regs[13] = 0x8ff8U;
    if (service.handle(
            memory,
            kA32LibDlDlcloseSvcImmediate,
            regs,
            cpsr) != A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        handles[0].refcount != 0U ||
        link_map.roots.size() != 1U ||
        link_map.roots[0].object_index != 2U ||
        link_map.object_states[0] != Elf32LinkMapObjectState::Retired ||
        link_map.object_states[1] != Elf32LinkMapObjectState::Active ||
        link_map.object_states[2] != Elf32LinkMapObjectState::Active ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Complete ||
        lifecycle.objects[1].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        !mapped(memory, link_map, 1U) ||
        !mapped(memory, link_map, 2U) ||
        mapped(memory, link_map, 0U)) {
        return fail("targeted service dlclose did not preserve shared root closure");
    }
    return 0;
}

int test_live_handle_retains_dependency() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    if (!stage_object(memory, link_map, "root", 0x18000U) ||
        !stage_object(memory, link_map, "dependency", 0x1A000U)) {
        return fail("could not map live-handle unload fixture");
    }
    link_map.graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{.requested_name = "dep.so", .target_object = 1U});
    link_map.roots = {
        Elf32LinkMapRoot{.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local},
    };
    link_map.object_states.assign(2U, Elf32LinkMapObjectState::Active);

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(2U);
    for (auto& state : lifecycle.objects) {
        state.constructors = Elf32LifecycleObjectStatus::Complete;
    }

    std::array<A32LibDlHandle, 2> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
        {.guest_handle = 0x70000004U, .object_index = 1U, .refcount = 1U},
    }};
    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> service_entries{};
    A32HostServiceRegistry registry{std::span{service_entries}};
    std::array<A32LibDlObjectLifecycleBinding, 2> bindings{{
        {.object_index = 0U, .dso_handle = 0xD000U},
        {.object_index = 1U, .dso_handle = 0xE000U},
    }};
    A32LibDlCloseTransaction finalizer{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span<const A32LibDlObjectLifecycleBinding>{bindings},
        close_options(registry),
    };
    A32LibDlUnloadTransaction unload{
        link_map,
        std::span{handles},
        lifecycle,
        finalizer,
        unload_options(),
    };

    const auto result = unload.close(memory, 0x70000000U, 0x8ff8U);
    if (!result ||
        result.outcome != A32LibDlUnloadTransactionOutcome::ObjectsUnloaded ||
        result.teardown_objects != std::vector<std::size_t>{0U} ||
        handles[0].refcount != 0U ||
        handles[1].refcount != 1U ||
        link_map.object_states[0] != Elf32LinkMapObjectState::Retired ||
        link_map.object_states[1] != Elf32LinkMapObjectState::Active ||
        lifecycle.objects[1].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        mapped(memory, link_map, 0U) ||
        !mapped(memory, link_map, 1U)) {
        return fail("live dependency handle did not retain dependency");
    }
    return 0;
}

int test_failure_preserves_ownership_and_retry_skips_completed_object() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    if (!stage_object(memory, link_map, "requester", 0x20000U) ||
        !stage_object(memory, link_map, "dependency", 0x22000U)) {
        return fail("could not map retry unload fixture");
    }
    link_map.graph.objects[0].dependencies.push_back(
        Elf32DependencyEdge{.requested_name = "dep.so", .target_object = 1U});
    link_map.roots = {
        Elf32LinkMapRoot{.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local},
    };
    link_map.object_states.assign(2U, Elf32LinkMapObjectState::Active);

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(2U);
    for (auto& state : lifecycle.objects) {
        state.constructors = Elf32LifecycleObjectStatus::Complete;
    }

    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
    }};
    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> service_entries{};
    A32HostServiceRegistry registry{std::span{service_entries}};
    std::array<A32LibDlObjectLifecycleBinding, 2> bindings{{
        {.object_index = 0U, .dso_handle = 0xF000U},
        {.object_index = 1U, .dso_handle = 0U},
    }};
    A32LibDlCloseTransaction finalizer{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span<const A32LibDlObjectLifecycleBinding>{bindings},
        close_options(registry),
    };
    A32LibDlUnloadTransaction unload{
        link_map,
        std::span{handles},
        lifecycle,
        finalizer,
        unload_options(),
    };

    const auto failed = unload.close(memory, 0x70000000U, 0x8ff8U);
    if (failed.error != A32LibDlUnloadTransactionError::FinalizationFailed ||
        failed.finalization_error != A32LibDlCloseTransactionError::InvalidBinding ||
        failed.failing_object != std::optional<std::size_t>{1U} ||
        failed.teardown_objects != std::vector<std::size_t>{0U, 1U} ||
        failed.objects_completed != 1U ||
        handles[0].refcount != 1U ||
        link_map.roots.size() != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Complete ||
        lifecycle.objects[1].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        !mapped(memory, link_map, 0U) ||
        !mapped(memory, link_map, 1U)) {
        return fail("failed targeted teardown did not preserve ownership");
    }

    bindings[1].dso_handle = 0x11000U;
    const auto retried = unload.close(memory, 0x70000000U, 0x8ff8U);
    if (!retried ||
        retried.teardown_objects != std::vector<std::size_t>{0U, 1U} ||
        retried.objects_completed != 1U ||
        handles[0].refcount != 0U ||
        !link_map.roots.empty() ||
        link_map.object_states !=
            std::vector<Elf32LinkMapObjectState>{
                Elf32LinkMapObjectState::Retired,
                Elf32LinkMapObjectState::Retired} ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Complete ||
        lifecycle.objects[1].destructors !=
            Elf32LifecycleObjectStatus::Complete ||
        mapped(memory, link_map, 0U) ||
        mapped(memory, link_map, 1U)) {
        return fail("targeted teardown retry replayed or failed to reclaim");
    }
    return 0;
}

int test_preexisting_unowned_object_blocks_targeted_close() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    if (!stage_object(memory, link_map, "root", 0x24000U) ||
        !stage_object(memory, link_map, "orphan", 0x26000U)) {
        return fail("could not map preexisting-orphan fixture");
    }
    link_map.roots = {
        Elf32LinkMapRoot{
            .object_index = 0U,
            .policy = Elf32LinkMapRootPolicy::Local},
    };
    link_map.object_states.assign(2U, Elf32LinkMapObjectState::Active);

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(2U);
    for (auto& state : lifecycle.objects) {
        state.constructors = Elf32LifecycleObjectStatus::Complete;
    }
    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 1U},
    }};
    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> service_entries{};
    A32HostServiceRegistry registry{std::span{service_entries}};
    const std::array<A32LibDlObjectLifecycleBinding, 2> bindings{{
        {.object_index = 0U, .dso_handle = 0x13000U},
        {.object_index = 1U, .dso_handle = 0x14000U},
    }};
    A32LibDlCloseTransaction finalizer{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{bindings},
        close_options(registry),
    };
    A32LibDlUnloadTransaction unload{
        link_map,
        std::span{handles},
        lifecycle,
        finalizer,
        unload_options(),
    };

    const auto result = unload.close(memory, 0x70000000U, 0x8ff8U);
    if (result.error !=
            A32LibDlUnloadTransactionError::PreexistingUnownedObjects ||
        result.failing_object != std::optional<std::size_t>{1U} ||
        handles[0].refcount != 1U ||
        link_map.roots.size() != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        lifecycle.objects[1].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        !mapped(memory, link_map, 0U) ||
        !mapped(memory, link_map, 1U)) {
        return fail("targeted close collected a pre-existing orphan");
    }
    return 0;
}

int test_nonfinal_reference_only_decrements() {
    MappedGuestMemory memory;
    Elf32LinkMap link_map;
    if (!stage_object(memory, link_map, "root", 0x28000U)) {
        return fail("could not map refcount unload fixture");
    }
    link_map.roots = {
        Elf32LinkMapRoot{.object_index = 0U, .policy = Elf32LinkMapRootPolicy::Local},
    };
    link_map.object_states = {Elf32LinkMapObjectState::Active};

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(1U);
    lifecycle.objects[0].constructors =
        Elf32LifecycleObjectStatus::Complete;
    std::array<A32LibDlHandle, 1> handles{{
        {.guest_handle = 0x70000000U, .object_index = 0U, .refcount = 2U},
    }};
    std::array<A32AeabiAtexitRecord, 1> records{};
    A32AeabiAtexitService registrations{std::span{records}};
    const std::array<A32HostServiceRegistryEntry, 0> service_entries{};
    A32HostServiceRegistry registry{std::span{service_entries}};
    const std::array<A32LibDlObjectLifecycleBinding, 1> bindings{{
        {.object_index = 0U, .dso_handle = 0x12000U},
    }};
    A32LibDlCloseTransaction finalizer{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{bindings},
        close_options(registry),
    };
    A32LibDlUnloadTransaction unload{
        link_map,
        std::span{handles},
        lifecycle,
        finalizer,
        unload_options(),
    };

    const auto result = unload.close(memory, 0x70000000U, 0x8ff8U);
    if (!result ||
        result.outcome !=
            A32LibDlUnloadTransactionOutcome::RefcountDecremented ||
        handles[0].refcount != 1U ||
        lifecycle.objects[0].destructors !=
            Elf32LifecycleObjectStatus::Pending ||
        !mapped(memory, link_map, 0U) ||
        link_map.roots.size() != 1U) {
        return fail("non-final targeted dlclose mutated ownership/lifecycle");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status =
            test_service_unloads_root_but_retains_shared_dependency();
        status != 0) {
        return status;
    }
    if (const int status = test_live_handle_retains_dependency();
        status != 0) {
        return status;
    }
    if (const int status =
            test_failure_preserves_ownership_and_retry_skips_completed_object();
        status != 0) {
        return status;
    }
    if (const int status =
            test_preexisting_unowned_object_blocks_targeted_close();
        status != 0) {
        return status;
    }
    return test_nonfinal_reference_only_decrements();
}
