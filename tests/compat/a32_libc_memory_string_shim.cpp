#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "compat/a32_android_namespace_policy.h"
#include "compat/a32_android_platform_provider.h"
#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libc_memory_string.h"
#include "compat/a32_libc_heap.h"
#include "compat/a32_pthread_sync.h"
#include "compat/a32_libc_integer.h"
#include "compat/a32_libc_memory_string_shim.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_lifecycle.h"
#include "elf/elf32_relocation.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AndroidNamespaceAccessPolicy;
using liba32android::compat::A32AndroidNamespaceBinding;
using liba32android::compat::A32AndroidNamespaceLink;
using liba32android::compat::A32AndroidPlatformCatalogProvider;
using liba32android::compat::A32AeabiAtexitRecord;
using liba32android::compat::A32AeabiAtexitRecordStatus;
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32AeabiFinalizeOptions;
using liba32android::compat::A32CxaFinalizeService;
using liba32android::compat::A32LibcGuestErrnoState;
using liba32android::compat::A32LibcGuestHeap;
using liba32android::compat::A32LibcHeapBlock;
using liba32android::compat::A32LibcHeapOptions;
using liba32android::compat::A32PthreadKeyState;
using liba32android::compat::A32PthreadMutexState;
using liba32android::compat::A32PthreadSyncService;
using liba32android::compat::A32PthreadTlsValue;
using liba32android::compat::A32PthreadWaiter;
using liba32android::compat::A32SemaphoreState;
using liba32android::compat::A32LibcIntegerOptions;
using liba32android::compat::A32LibcIntegerService;
using liba32android::compat::A32LibcMemoryStringOptions;
using liba32android::compat::A32LibcMemoryStringService;
using liba32android::compat::kA32AeabiAtexitSvcImmediate;
using liba32android::compat::kA32CxaFinalizeSvcImmediate;
using liba32android::compat::kA32LibcMemchrSvcImmediate;
using liba32android::compat::kA32LibcMemcmpSvcImmediate;
using liba32android::compat::kA32LibcMemcpySvcImmediate;
using liba32android::compat::kA32LibcMemmoveSvcImmediate;
using liba32android::compat::kA32AndroidErange;
using liba32android::compat::kA32LibcAtoiSvcImmediate;
using liba32android::compat::kA32LibcCallocSvcImmediate;
using liba32android::compat::kA32LibcFreeSvcImmediate;
using liba32android::compat::kA32LibcMallocSvcImmediate;
using liba32android::compat::kA32LibcReallocSvcImmediate;
using liba32android::compat::kA32LibcErrnoSvcImmediate;
using liba32android::compat::kA32LibcStrtolSvcImmediate;
using liba32android::compat::kA32LibcMemoryStringShimIdentity;
using liba32android::compat::kA32LibcMemoryStringShimSoname;
using liba32android::compat::kA32LibcMemsetSvcImmediate;
using liba32android::compat::kA32LibcMemmemSvcImmediate;
using liba32android::compat::kA32LibcStrcpySvcImmediate;
using liba32android::compat::kA32LibcStrncpySvcImmediate;
using liba32android::compat::kA32LibcStrcmpSvcImmediate;
using liba32android::compat::kA32LibcStrlenSvcImmediate;
using liba32android::compat::kA32LibcStrncmpSvcImmediate;
using liba32android::compat::kA32AndroidEbusy;
using liba32android::compat::kA32PthreadMutexInitSvcImmediate;
using liba32android::compat::kA32PthreadMutexDestroySvcImmediate;
using liba32android::compat::kA32PthreadMutexLockSvcImmediate;
using liba32android::compat::kA32PthreadMutexTrylockSvcImmediate;
using liba32android::compat::kA32PthreadMutexUnlockSvcImmediate;
using liba32android::compat::kA32PthreadKeyCreateSvcImmediate;
using liba32android::compat::kA32PthreadKeyDeleteSvcImmediate;
using liba32android::compat::kA32PthreadGetspecificSvcImmediate;
using liba32android::compat::kA32PthreadSetspecificSvcImmediate;
using liba32android::compat::kA32SemInitSvcImmediate;
using liba32android::compat::kA32SemDestroySvcImmediate;
using liba32android::compat::kA32SemWaitSvcImmediate;
using liba32android::compat::kA32SemPostSvcImmediate;
using liba32android::compat::make_a32_libc_memory_string_shim_catalog_entry;
using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderChain;
using liba32android::elf::Elf32FiniExecutionOptions;
using liba32android::elf::Elf32FiniPlanOptions;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::execute_elf32_fini_calls;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::plan_elf32_fini_array_calls;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::execute_a32_with_services;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::size_t kInstructionBudget = 256;
constexpr std::size_t kStackPages = 4;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

std::int32_t signed_r0(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t read_u32_le(
    const MappedGuestMemory& memory,
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

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 512,
        .max_hash_buckets = 512,
        .max_gnu_bloom_words = 128,
        .max_scope_objects = 8,
        .max_name_bytes = kMaxFixtureNameBytes,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 64;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("libc shim symbol lookup failed for ") +
           std::string(name) + ": graph=" +
           liba32android::elf::to_string(result.error) + ", index=" +
           liba32android::elf::to_string(result.index_error) + ", lookup=" +
           liba32android::elf::to_string(result.lookup_error) + ", string=" +
           liba32android::elf::to_string(result.string_error);
}

std::optional<std::uint32_t> find_unmapped_region(
    const MappedGuestMemory& memory,
    std::uint32_t start,
    std::size_t page_count) {
    const std::uint64_t page_size = memory.page_size();
    const std::uint64_t length = page_size * page_count;
    if (page_count == 0 || length > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    for (std::uint64_t candidate = start;
         candidate + length <= 0xf0000000ULL;
         candidate += page_size * 16U) {
        bool available = true;
        for (std::size_t i = 0; i < page_count; ++i) {
            const std::uint64_t page = candidate + i * page_size;
            if (page > std::numeric_limits<std::uint32_t>::max() ||
                memory.is_mapped(static_cast<std::uint32_t>(page))) {
                available = false;
                break;
            }
        }
        if (available) {
            return static_cast<std::uint32_t>(candidate);
        }
    }
    return std::nullopt;
}

std::optional<A32ServiceDispatchResult> run_wrapper(
    MappedGuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::string_view name,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::uint32_t r0,
    std::uint32_t r1,
    std::uint32_t r2,
    std::uint32_t r3 = 0) {
    const auto lookup = lookup_elf32_graph_symbol(
        memory, graph, 0, name, symbol_options());
    if (!lookup) {
        std::cerr << lookup_error(name, lookup) << '\n';
        return std::nullopt;
    }
    if (lookup.symbol.object_index != 0 ||
        lookup.symbol.symbol.symbol.type != 2U ||
        lookup.symbol.symbol.symbol.size == 0) {
        std::cerr << "wrapper symbol was not a non-empty root STT_FUNC: "
                  << name << '\n';
        return std::nullopt;
    }

    const auto& symbol = lookup.symbol.symbol;
    const bool thumb = (symbol.symbol.value & 1U) != 0;
    ExecutionRequest request{};
    request.instruction_set =
        thumb ? InstructionSet::Thumb : InstructionSet::Arm;
    request.entry_pc = symbol.guest_value & ~1U;
    request.regs[0] = r0;
    request.regs[1] = r1;
    request.regs[2] = r2;
    request.regs[3] = r3;
    request.regs[13] = stack_top;
    request.regs[14] = stop_pc | (thumb ? 1U : 0U);
    request.instruction_count = kInstructionBudget;
    request.stop_pc = stop_pc;

    return execute_a32_with_services(memory, request, registry, 1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail("expected paths to ARM32 libc consumer and partial libc.so shim");
    }

    const std::vector<std::uint8_t> consumer_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> shim_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (consumer_image.empty() || shim_image.empty()) {
        return fail("generated ARM32 libc memory/string fixture is missing or empty");
    }

    MappedGuestMemory memory;

    const std::array<Elf32DependencyCatalogEntry, 0> app_entries{};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};

    const std::array<Elf32DependencyCatalogEntry, 1> platform_entries{{
        make_a32_libc_memory_string_shim_catalog_entry(shim_image),
    }};
    const std::array<A32AndroidNamespaceBinding, 1> namespace_bindings{{
        {"libc-memory-string-consumer", "app"},
    }};
    const std::array<std::string_view, 1> platform_shared_libs{{
        kA32LibcMemoryStringShimSoname,
    }};
    const std::array<A32AndroidNamespaceLink, 1> namespace_links{{
        {
            "app",
            "platform",
            false,
            std::span{platform_shared_libs},
        },
    }};
    A32AndroidNamespaceAccessPolicy namespace_policy{
        std::span{namespace_bindings},
        std::span{namespace_links},
        "platform",
    };
    A32AndroidPlatformCatalogProvider platform_provider{
        std::span{platform_entries}, namespace_policy};

    const std::array<Elf32DependencyProvider*, 2> provider_list{{
        &app_provider,
        &platform_provider,
    }};
    Elf32DependencyProviderChain provider_chain{std::span{provider_list}};

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 8;
    load_options.max_depth = 8;
    load_options.max_dependency_occurrences = 8;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxTotalImageBytes;
    load_options.max_string_bytes = kMaxFixtureNameBytes;

    const auto graph_result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "libc-memory-string-consumer",
            .image = consumer_image,
        },
        provider_chain,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("libc shim dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 2) {
        return fail("libc namespace-gated platform catalog did not form two-object graph");
    }

    const auto& consumer = graph_result.graph.objects[0];
    const auto& shim = graph_result.graph.objects[1];
    if (consumer.linker_strings.needed.size() != 1 ||
        consumer.linker_strings.needed[0] != kA32LibcMemoryStringShimSoname ||
        consumer.dependencies.size() != 1 ||
        consumer.dependencies[0].requested_name !=
            kA32LibcMemoryStringShimSoname ||
        consumer.dependencies[0].target_object != 1 ||
        shim.identity != kA32LibcMemoryStringShimIdentity ||
        !shim.linker_strings.needed.empty()) {
        return fail("libc shim dependency/provider metadata was incorrect");
    }

    constexpr std::array<std::string_view, 46> shim_names{{
        "memcpy", "memmove", "memset", "memcmp", "memchr",
        "strlen", "strcmp", "strncmp",
        "memmem", "strcpy", "strncpy",
        "atoi", "strtol", "__errno",
        "malloc", "calloc", "realloc", "free",
        "__aeabi_atexit", "__cxa_atexit", "__cxa_finalize",
        "__aeabi_memcpy", "__aeabi_memcpy4", "__aeabi_memcpy8",
        "__aeabi_memmove", "__aeabi_memmove4", "__aeabi_memmove8",
        "__aeabi_memset", "__aeabi_memset4", "__aeabi_memset8",
        "__aeabi_memclr", "__aeabi_memclr4", "__aeabi_memclr8",
        "pthread_mutex_init", "pthread_mutex_destroy",
        "pthread_mutex_lock", "pthread_mutex_trylock",
        "pthread_mutex_unlock", "sem_init", "sem_destroy",
        "sem_wait", "sem_post",
        "pthread_key_create", "pthread_key_delete",
        "pthread_getspecific", "pthread_setspecific",
    }};
    std::array<std::uint32_t, shim_names.size()> shim_targets{};
    for (std::size_t i = 0; i < shim_names.size(); ++i) {
        const auto lookup = lookup_elf32_graph_symbol(
            memory, graph_result.graph, 0, shim_names[i], symbol_options());
        if (!lookup) {
            return fail(lookup_error(shim_names[i], lookup));
        }
        if (lookup.symbol.object_index != 1 ||
            lookup.symbol.symbol.symbol.type != 2U) {
            return fail("libc import did not resolve to shim STT_FUNC");
        }
        shim_targets[i] = lookup.symbol.symbol.guest_value;
    }

    const auto relocated = apply_elf32_combined_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!relocated) {
        return fail(
            std::string("libc shim consumer relocation failed: ") +
            liba32android::elf::to_string(relocated.error));
    }
    for (const std::uint32_t target : shim_targets) {
        bool found = false;
        for (const auto& write : relocated.application.writes) {
            if (write.type == kRArmJumpSlot &&
                write.final_word == target) {
                found = true;
                break;
            }
        }
        if (!found) {
            return fail("libc consumer missing expected JUMP_SLOT target");
        }
    }

    const auto data = find_unmapped_region(memory, 0x70000000U, 1);
    const auto stack = find_unmapped_region(memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(memory, 0x72000000U, 1);
    const auto heap_region = find_unmapped_region(memory, 0x73000000U, 1);
    if (!data.has_value() || !stack.has_value() || !stop.has_value() ||
        !heap_region.has_value()) {
        return fail("could not reserve libc shim execution harness regions");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*data, memory.page_size(), rw) ||
        !memory.map(*stack, memory.page_size() * kStackPages, rw) ||
        !memory.map(*heap_region, memory.page_size(), rw)) {
        return fail("could not map libc shim data/stack/heap");
    }

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("libc shim stack top overflowed guest address space");
    }
    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(stack_top64) & ~7U;

    constexpr std::array<std::uint8_t, 4> source{{1, 2, 3, 4}};
    constexpr std::array<std::uint8_t, 4> rhs{{1, 2, 4, 4}};
    constexpr std::array<std::uint8_t, 6> alpha{
        'a','l','p','h','a',0,
    };
    constexpr std::array<std::uint8_t, 6> alphb{
        'a','l','p','h','b',0,
    };
    constexpr std::array<std::uint8_t, 4> atoi_input{
        '1','2','3',0,
    };
    constexpr std::array<std::uint8_t, 12> strtol_input{
        '2','1','4','7','4','8','3','6','4','8','x',0,
    };
    const std::uint32_t source_address = *data;
    const std::uint32_t destination_address = *data + 0x40U;
    const std::uint32_t rhs_address = *data + 0x80U;
    const std::uint32_t alpha_address = *data + 0x100U;
    const std::uint32_t alphb_address = *data + 0x120U;
    const std::uint32_t atoi_address = *data + 0x220U;
    const std::uint32_t strtol_address = *data + 0x240U;
    const std::uint32_t endptr_address = *data + 0x260U;
    const std::uint32_t errno_address = *data + 0x280U;
    constexpr std::array<std::uint8_t, 4> zero_errno{{0,0,0,0}};
    if (!memory.write(source_address, source) ||
        !memory.write(rhs_address, rhs) ||
        !memory.write(alpha_address, alpha) ||
        !memory.write(alphb_address, alphb) ||
        !memory.write(atoi_address, atoi_input) ||
        !memory.write(strtol_address, strtol_input) ||
        !memory.write(errno_address, zero_errno)) {
        return fail("could not stage libc shim guest inputs");
    }

    A32LibcMemoryStringService service{
        A32LibcMemoryStringOptions{64, 64}};
    A32LibcGuestErrnoState errno_state{errno_address};
    A32LibcIntegerService integer_service{
        errno_state, A32LibcIntegerOptions{64}};
    std::array<A32LibcHeapBlock, 8> heap_metadata{};
    A32LibcGuestHeap heap{
        errno_state,
        A32LibcHeapOptions{
            *heap_region,
            static_cast<std::uint64_t>(*heap_region) + memory.page_size()},
        std::span{heap_metadata},
    };
    std::array<A32PthreadMutexState, 8> mutex_metadata{};
    std::array<A32SemaphoreState, 8> semaphore_metadata{};
    std::array<A32PthreadWaiter, 16> sync_waiters{};
    std::array<A32PthreadKeyState, 8> pthread_keys{};
    std::array<A32PthreadTlsValue, 16> pthread_tls_values{};
    A32PthreadSyncService sync_service{
        std::span{mutex_metadata},
        std::span{semaphore_metadata},
        std::span{sync_waiters},
        std::span{pthread_keys},
        std::span{pthread_tls_values},
    };
    sync_service.set_current_thread_id(1U);
    std::array<A32AeabiAtexitRecord, 8> atexit_records{};
    A32AeabiAtexitService atexit_service{std::span{atexit_records}};
    A32CxaFinalizeService cxa_finalize_service{
        atexit_service,
        A32AeabiFinalizeOptions{
            .stack_top = stack_top,
            .return_pc = *stop,
            .max_instructions_per_call = kInstructionBudget,
            .max_callbacks = 8U,
        },
    };

    const std::array<A32HostServiceRegistryEntry, 33> services{{
        {kA32LibcMemcpySvcImmediate, &service},
        {kA32LibcMemmoveSvcImmediate, &service},
        {kA32LibcMemsetSvcImmediate, &service},
        {kA32LibcMemcmpSvcImmediate, &service},
        {kA32LibcMemchrSvcImmediate, &service},
        {kA32LibcStrlenSvcImmediate, &service},
        {kA32LibcStrcmpSvcImmediate, &service},
        {kA32LibcStrncmpSvcImmediate, &service},
        {kA32LibcMemmemSvcImmediate, &service},
        {kA32LibcStrcpySvcImmediate, &service},
        {kA32LibcStrncpySvcImmediate, &service},
        {kA32LibcAtoiSvcImmediate, &integer_service},
        {kA32LibcStrtolSvcImmediate, &integer_service},
        {kA32LibcErrnoSvcImmediate, &errno_state},
        {kA32LibcMallocSvcImmediate, &heap},
        {kA32LibcCallocSvcImmediate, &heap},
        {kA32LibcReallocSvcImmediate, &heap},
        {kA32LibcFreeSvcImmediate, &heap},
        {kA32PthreadMutexInitSvcImmediate, &sync_service},
        {kA32PthreadMutexDestroySvcImmediate, &sync_service},
        {kA32PthreadMutexLockSvcImmediate, &sync_service},
        {kA32PthreadMutexTrylockSvcImmediate, &sync_service},
        {kA32PthreadMutexUnlockSvcImmediate, &sync_service},
        {kA32SemInitSvcImmediate, &sync_service},
        {kA32SemDestroySvcImmediate, &sync_service},
        {kA32SemWaitSvcImmediate, &sync_service},
        {kA32SemPostSvcImmediate, &sync_service},
        {kA32PthreadKeyCreateSvcImmediate, &sync_service},
        {kA32PthreadKeyDeleteSvcImmediate, &sync_service},
        {kA32PthreadGetspecificSvcImmediate, &sync_service},
        {kA32PthreadSetspecificSvcImmediate, &sync_service},
        {kA32AeabiAtexitSvcImmediate, &atexit_service},
        {kA32CxaFinalizeSvcImmediate, &cxa_finalize_service},
    }};
    A32HostServiceRegistry registry{std::span{services}};

    std::size_t completed_calls = 0;
    std::size_t fini_array_calls = 0;

    auto result = run_wrapper(
        memory, graph_result.graph, "fixture_memcpy", registry,
        stack_top, *stop, destination_address, source_address, 4);
    if (!result || !*result || !result->stop_pc_reached ||
        result->services_handled != 1 ||
        result->regs[0] != destination_address) {
        return fail("real libc memcpy wrapper execution failed");
    }
    ++completed_calls;
    std::array<std::uint8_t, 4> copied{};
    if (!memory.read(destination_address, copied) || copied != source) {
        return fail("real libc memcpy wrapper did not copy bytes");
    }


    const std::uint32_t move_address = *data + 0x300U;
    constexpr std::array<std::uint8_t, 8> move_input{{1,2,3,4,5,6,7,8}};
    constexpr std::array<std::uint8_t, 8> move_expected{{1,2,1,2,3,4,5,6}};
    if (!memory.write(move_address, move_input)) {
        return fail("could not stage real libc memmove input");
    }
    result = run_wrapper(
        memory, graph_result.graph, "fixture_memmove", registry,
        stack_top, *stop, move_address + 2U, move_address, 6U);
    std::array<std::uint8_t, 8> move_observed{};
    if (!result || !*result || result->services_handled != 1 ||
        result->regs[0] != move_address + 2U ||
        !memory.read(move_address, move_observed) ||
        move_observed != move_expected) {
        return fail("real libc memmove wrapper overlap semantics failed");
    }
    ++completed_calls;

    const std::uint32_t helper_source = *data + 0x340U;
    const std::uint32_t helper_destination = *data + 0x380U;
    constexpr std::array<std::uint8_t, 6> helper_bytes{{9,8,7,6,5,4}};
    constexpr std::array<std::uint8_t, 6> helper_zero{{0,0,0,0,0,0}};
    if (!memory.write(helper_source, helper_bytes)) {
        return fail("could not stage EABI helper source");
    }

    constexpr std::array<std::string_view, 3> aeabi_memcpy_wrappers{{
        "fixture_aeabi_memcpy",
        "fixture_aeabi_memcpy4",
        "fixture_aeabi_memcpy8",
    }};
    for (const auto name : aeabi_memcpy_wrappers) {
        if (!memory.write(helper_destination, helper_zero)) {
            return fail("could not reset EABI memcpy destination");
        }
        result = run_wrapper(
            memory, graph_result.graph, name, registry,
            stack_top, *stop, helper_destination, helper_source,
            static_cast<std::uint32_t>(helper_bytes.size()));
        std::array<std::uint8_t, helper_bytes.size()> observed_helper{};
        if (!result || !*result || result->services_handled != 1 ||
            !memory.read(helper_destination, observed_helper) ||
            observed_helper != helper_bytes) {
            return fail("real EABI memcpy wrapper failed");
        }
        ++completed_calls;
    }

    const std::uint32_t helper_move = *data + 0x3c0U;
    constexpr std::array<std::string_view, 3> aeabi_memmove_wrappers{{
        "fixture_aeabi_memmove",
        "fixture_aeabi_memmove4",
        "fixture_aeabi_memmove8",
    }};
    for (const auto name : aeabi_memmove_wrappers) {
        if (!memory.write(helper_move, move_input)) {
            return fail("could not reset EABI memmove source");
        }
        result = run_wrapper(
            memory, graph_result.graph, name, registry,
            stack_top, *stop, helper_move + 2U, helper_move, 6U);
        move_observed = {};
        if (!result || !*result || result->services_handled != 1 ||
            !memory.read(helper_move, move_observed) ||
            move_observed != move_expected) {
            return fail("real EABI memmove wrapper failed");
        }
        ++completed_calls;
    }

    constexpr std::array<std::string_view, 3> aeabi_memset_wrappers{{
        "fixture_aeabi_memset",
        "fixture_aeabi_memset4",
        "fixture_aeabi_memset8",
    }};
    constexpr std::array<std::uint8_t, 3> helper_filled{{0x5a,0x5a,0x5a}};
    for (const auto name : aeabi_memset_wrappers) {
        if (!memory.write(helper_destination, helper_zero)) {
            return fail("could not reset EABI memset destination");
        }
        result = run_wrapper(
            memory, graph_result.graph, name, registry,
            stack_top, *stop, helper_destination, 3U, 0x5aU);
        std::array<std::uint8_t, 3> observed_filled{};
        if (!result || !*result || result->services_handled != 1 ||
            !memory.read(helper_destination, observed_filled) ||
            observed_filled != helper_filled) {
            return fail("real EABI memset wrapper argument order failed");
        }
        ++completed_calls;
    }

    constexpr std::array<std::string_view, 3> aeabi_memclr_wrappers{{
        "fixture_aeabi_memclr",
        "fixture_aeabi_memclr4",
        "fixture_aeabi_memclr8",
    }};
    constexpr std::array<std::uint8_t, 4> helper_dirty{{1,2,3,4}};
    constexpr std::array<std::uint8_t, 4> helper_cleared{{0,0,0,0}};
    for (const auto name : aeabi_memclr_wrappers) {
        if (!memory.write(helper_destination, helper_dirty)) {
            return fail("could not dirty EABI memclr destination");
        }
        result = run_wrapper(
            memory, graph_result.graph, name, registry,
            stack_top, *stop, helper_destination, 4U, 0U);
        std::array<std::uint8_t, 4> observed_cleared{};
        if (!result || !*result || result->services_handled != 1 ||
            !memory.read(helper_destination, observed_cleared) ||
            observed_cleared != helper_cleared) {
            return fail("real EABI memclr wrapper failed");
        }
        ++completed_calls;
    }

    result = run_wrapper(
        memory, graph_result.graph, "fixture_memset", registry,
        stack_top, *stop, destination_address + 4U, 0xAAU, 2);
    if (!result || !*result || result->regs[0] != destination_address + 4U) {
        return fail("real libc memset wrapper execution failed");
    }
    ++completed_calls;
    std::array<std::uint8_t, 2> filled{};
    if (!memory.read(destination_address + 4U, filled) ||
        filled != std::array<std::uint8_t, 2>{{0xAA, 0xAA}}) {
        return fail("real libc memset wrapper did not write bytes");
    }

    result = run_wrapper(
        memory, graph_result.graph, "fixture_memcmp", registry,
        stack_top, *stop, source_address, rhs_address, 4);
    if (!result || !*result || signed_r0(result->regs[0]) >= 0) {
        return fail("real libc memcmp wrapper sign failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_memchr", registry,
        stack_top, *stop, source_address, 3U, 4U);
    if (!result || !*result || result->regs[0] != source_address + 2U) {
        return fail("real libc memchr wrapper pointer failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_strlen", registry,
        stack_top, *stop, alpha_address, 0, 0);
    if (!result || !*result || result->regs[0] != 5U) {
        return fail("real libc strlen wrapper result failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_strcmp", registry,
        stack_top, *stop, alpha_address, alphb_address, 0);
    if (!result || !*result || signed_r0(result->regs[0]) >= 0) {
        return fail("real libc strcmp wrapper sign failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_strncmp", registry,
        stack_top, *stop, alpha_address, alphb_address, 4U);
    if (!result || !*result || signed_r0(result->regs[0]) != 0) {
        return fail("real libc strncmp wrapper prefix result failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_memmem", registry,
        stack_top, *stop, alpha_address, 5U, alphb_address + 1U, 2U);
    if (!result || !*result || result->regs[0] != alpha_address + 1U) {
        return fail("real libc memmem wrapper pointer failed");
    }
    ++completed_calls;

    const std::uint32_t strcpy_destination = *data + 0x180U;
    result = run_wrapper(
        memory, graph_result.graph, "fixture_strcpy", registry,
        stack_top, *stop, strcpy_destination, alpha_address, 0U);
    if (!result || !*result || result->regs[0] != strcpy_destination) {
        return fail("real libc strcpy wrapper result failed");
    }
    std::array<std::uint8_t, 6> strcpy_bytes{};
    if (!memory.read(strcpy_destination, strcpy_bytes) ||
        strcpy_bytes != alpha) {
        return fail("real libc strcpy wrapper bytes failed");
    }
    ++completed_calls;

    const std::uint32_t strncpy_destination = *data + 0x1c0U;
    result = run_wrapper(
        memory, graph_result.graph, "fixture_strncpy", registry,
        stack_top, *stop, strncpy_destination, alpha_address, 4U);
    if (!result || !*result || result->regs[0] != strncpy_destination) {
        return fail("real libc strncpy wrapper result failed");
    }
    std::array<std::uint8_t, 4> strncpy_bytes{};
    if (!memory.read(strncpy_destination, strncpy_bytes) ||
        strncpy_bytes != std::array<std::uint8_t, 4>{{'a','l','p','h'}}) {
        return fail("real libc strncpy wrapper truncation failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_atoi", registry,
        stack_top, *stop, atoi_address, 0U, 0U);
    if (!result || !*result || signed_r0(result->regs[0]) != 123) {
        return fail("real libc atoi wrapper result failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_strtol", registry,
        stack_top, *stop, strtol_address, endptr_address, 10U);
    if (!result || !*result ||
        signed_r0(result->regs[0]) != std::numeric_limits<std::int32_t>::max() ||
        read_u32_le(memory, endptr_address) != strtol_address + 10U ||
        read_u32_le(memory, errno_address) !=
            static_cast<std::uint32_t>(kA32AndroidErange)) {
        return fail("real libc strtol overflow/endptr/errno publication failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_errno_read", registry,
        stack_top, *stop, 0U, 0U, 0U);
    if (!result || !*result ||
        signed_r0(result->regs[0]) != kA32AndroidErange) {
        return fail("real libc __errno wrapper did not expose guest errno slot");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_malloc", registry,
        stack_top, *stop, 8U, 0U, 0U);
    if (!result || !*result || result->regs[0] == 0U ||
        result->regs[0] < *heap_region ||
        static_cast<std::uint64_t>(result->regs[0]) >=
            static_cast<std::uint64_t>(*heap_region) + memory.page_size() ||
        (result->regs[0] % liba32android::compat::kA32AndroidMallocAlignment) != 0U) {
        return fail("real libc malloc wrapper returned invalid guest pointer");
    }
    const std::uint32_t malloc_address = result->regs[0];
    constexpr std::array<std::uint8_t, 8> heap_payload{{9,8,7,6,5,4,3,2}};
    if (!memory.write(malloc_address, heap_payload)) {
        return fail("could not stage real libc realloc payload");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_realloc", registry,
        stack_top, *stop, malloc_address, 32U, 0U);
    if (!result || !*result || result->regs[0] == 0U) {
        return fail("real libc realloc wrapper failed");
    }
    const std::uint32_t realloc_address = result->regs[0];
    std::array<std::uint8_t, heap_payload.size()> realloc_payload{};
    if (!memory.read(realloc_address, realloc_payload) ||
        realloc_payload != heap_payload) {
        return fail("real libc realloc wrapper did not preserve payload");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_calloc", registry,
        stack_top, *stop, 3U, 5U, 0U);
    if (!result || !*result || result->regs[0] == 0U) {
        return fail("real libc calloc wrapper failed");
    }
    std::array<std::uint8_t, 15> calloc_bytes{};
    std::array<std::uint8_t, 15> calloc_observed{};
    if (!memory.read(result->regs[0], calloc_observed) ||
        calloc_observed != calloc_bytes) {
        return fail("real libc calloc wrapper did not zero requested bytes");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_free", registry,
        stack_top, *stop, realloc_address, 0U, 0U);
    if (!result || !*result) {
        return fail("real libc free wrapper failed");
    }
    ++completed_calls;


    const std::uint32_t atexit_object = *data + 0x580U;
    constexpr std::array<std::uint8_t, 4> zero_finalize_marker{{0,0,0,0}};
    if (!memory.write(atexit_object, zero_finalize_marker)) {
        return fail("could not clear registered destructor marker");
    }

    // Let the ARM32 consumer compute both the destructor function value and
    // &fixture_dso_handle, then register them through __cxa_atexit's
    // (destructor, object, dso_handle) ABI. The shim reorders those words into
    // the shared bounded registration service. This is the same guest
    // relocation path later used by
    // fixture_on_dlclose -> __cxa_finalize(&fixture_dso_handle), so the
    // registration selector and FINI selector cannot diverge due to a
    // host-side symbol-address assumption.
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_register_static_destructor",
        registry,
        stack_top,
        *stop,
        atexit_object,
        0U,
        0U);
    if (!result || !*result ||
        result->services_handled != 1U ||
        result->regs[0] != 0U ||
        atexit_service.record_count() != 1U ||
        atexit_service.records()[0].object != atexit_object ||
        atexit_service.records()[0].destructor == 0U ||
        atexit_service.records()[0].dso_handle == 0U) {
        return fail("real guest static-destructor registration failed");
    }
    const std::uint32_t atexit_destructor =
        atexit_service.records()[0].destructor;
    const std::uint32_t atexit_dso_handle =
        atexit_service.records()[0].dso_handle;
    ++completed_calls;

    const auto fini_plan = plan_elf32_fini_array_calls(
        memory,
        graph_result.graph,
        0U,
        Elf32FiniPlanOptions{
            .max_objects = 2U,
            .max_entries = 4U,
        });
    if (!fini_plan || fini_plan.calls.size() != 1U) {
        return fail("real libc consumer did not expose one controlled FINI_ARRAY call");
    }
    const auto fini_executed = execute_elf32_fini_calls(
        memory,
        fini_plan.calls,
        Elf32FiniExecutionOptions{
            .stack_top = stack_top,
            .return_pc = *stop,
            .max_instructions_per_call = kInstructionBudget,
            .service_handler = &registry,
            .max_service_calls_per_call = 2U,
        });
    if (!fini_executed ||
        fini_executed.calls_completed != 1U ||
        read_u32_le(memory, atexit_object) != 0xC0DEC0DEU ||
        atexit_service.records()[0].status !=
            A32AeabiAtexitRecordStatus::Complete ||
        !cxa_finalize_service.last_result().has_value() ||
        !*cxa_finalize_service.last_result() ||
        cxa_finalize_service.last_result()->callbacks_completed != 1U) {
        std::cerr
            << "service-aware FINI diagnostic: lifecycle_error="
            << static_cast<unsigned>(fini_executed.error)
            << " calls=" << fini_executed.calls_completed
            << " svc="
            << fini_executed.failing_svc_immediate.value_or(0U)
            << " marker=" << read_u32_le(memory, atexit_object)
            << " record_status="
            << static_cast<unsigned>(atexit_service.records()[0].status)
            << " destructor=" << atexit_destructor
            << " dso=" << atexit_dso_handle;
        if (cxa_finalize_service.last_result().has_value()) {
            std::cerr
                << " finalize_error="
                << static_cast<unsigned>(
                    cxa_finalize_service.last_result()->error)
                << " finalize_callbacks="
                << cxa_finalize_service.last_result()->callbacks_completed;
        } else {
            std::cerr << " finalize_result=missing";
        }
        std::cerr << '\n';
        return fail("service-aware FINI_ARRAY did not run linked __cxa_finalize path");
    }
    fini_array_calls = fini_executed.calls_completed;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_cxa_finalize", registry,
        stack_top, *stop, atexit_dso_handle, 0U, 0U);
    if (!result || !*result ||
        result->services_handled != 1U ||
        !cxa_finalize_service.last_result().has_value() ||
        !*cxa_finalize_service.last_result() ||
        cxa_finalize_service.last_result()->callbacks_completed != 0U) {
        return fail("direct __cxa_finalize wrapper did not preserve once-only state");
    }
    ++completed_calls;

    const std::uint32_t mutex_address = *data + 0x500U;
    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_mutex_init", registry,
        stack_top, *stop, mutex_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_mutex_init wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_mutex_lock", registry,
        stack_top, *stop, mutex_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_mutex_lock wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_mutex_trylock", registry,
        stack_top, *stop, mutex_address, 0U, 0U);
    if (!result || !*result ||
        signed_r0(result->regs[0]) != kA32AndroidEbusy) {
        return fail("real pthread_mutex_trylock wrapper did not return EBUSY");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_mutex_unlock", registry,
        stack_top, *stop, mutex_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_mutex_unlock wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_mutex_destroy", registry,
        stack_top, *stop, mutex_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_mutex_destroy wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t semaphore_address = *data + 0x540U;
    result = run_wrapper(
        memory, graph_result.graph, "fixture_sem_init", registry,
        stack_top, *stop, semaphore_address, 0U, 1U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real sem_init wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_sem_wait", registry,
        stack_top, *stop, semaphore_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real sem_wait wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_sem_post", registry,
        stack_top, *stop, semaphore_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real sem_post wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_sem_destroy", registry,
        stack_top, *stop, semaphore_address, 0U, 0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real sem_destroy wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t pthread_key_address = *data + 0xe00U;
    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_key_create", registry,
        stack_top, *stop, pthread_key_address, 0U, 0U);
    std::array<std::uint8_t, 4> pthread_key_bytes{};
    if (!result || !*result || result->services_handled != 1 ||
        result->regs[0] != 0U ||
        !memory.read(pthread_key_address, pthread_key_bytes)) {
        return fail("real pthread_key_create wrapper failed");
    }
    ++completed_calls;
    const std::uint32_t pthread_key =
        static_cast<std::uint32_t>(pthread_key_bytes[0]) |
        (static_cast<std::uint32_t>(pthread_key_bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(pthread_key_bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(pthread_key_bytes[3]) << 24U);
    if (pthread_key == 0U) {
        return fail("real pthread_key_create returned zero logical key");
    }

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_setspecific", registry,
        stack_top, *stop, pthread_key, alpha_address, 0U);
    if (!result || !*result || result->services_handled != 1 ||
        result->regs[0] != 0U) {
        return fail("real pthread_setspecific wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_getspecific", registry,
        stack_top, *stop, pthread_key, 0U, 0U);
    if (!result || !*result || result->services_handled != 1 ||
        result->regs[0] != alpha_address) {
        return fail("real pthread_getspecific wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, graph_result.graph, "fixture_pthread_key_delete", registry,
        stack_top, *stop, pthread_key, 0U, 0U);
    if (!result || !*result || result->services_handled != 1 ||
        result->regs[0] != 0U) {
        return fail("real pthread_key_delete wrapper failed");
    }
    ++completed_calls;

    std::cout
        << "fixture.libc.object_count=" << graph_result.graph.objects.size() << '\n'
        << "fixture.libc.needed=" << kA32LibcMemoryStringShimSoname << '\n'
        << "fixture.libc.namespace_access=linked\n"
        << "fixture.libc.required_jump_slots=" << shim_targets.size() << '\n'
        << "fixture.libc.completed_service_calls=" << completed_calls << '\n'
        << "fixture.libc.fini_array_calls=" << fini_array_calls << '\n'
        << "fixture.libc.status=PASS\n";
    return 0;
}
