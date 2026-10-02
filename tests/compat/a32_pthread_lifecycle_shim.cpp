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
#include "compat/a32_libc_memory_string_shim.h"
#include "compat/a32_pthread_lifecycle.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_dependency_loader.h"
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
using liba32android::compat::A32PthreadAttrState;
using liba32android::compat::A32PthreadClock;
using liba32android::compat::A32PthreadClockId;
using liba32android::compat::A32PthreadKeyState;
using liba32android::compat::A32PthreadMutexState;
using liba32android::compat::A32PthreadSyncService;
using liba32android::compat::A32PthreadTlsValue;
using liba32android::compat::A32PthreadWaitKind;
using liba32android::compat::A32PthreadWaiter;
using liba32android::compat::A32SemaphoreState;
using liba32android::compat::A32PthreadLifecycleOptions;
using liba32android::compat::A32PthreadLifecycleService;
using liba32android::compat::A32PthreadThreadPhase;
using liba32android::compat::A32PthreadThreadState;
using liba32android::compat::kA32LibcMemoryStringShimIdentity;
using liba32android::compat::kA32LibcMemoryStringShimSoname;
using liba32android::compat::kA32PthreadAttrDestroySvcImmediate;
using liba32android::compat::kA32PthreadAttrGetdetachstateSvcImmediate;
using liba32android::compat::kA32PthreadAttrGetstacksizeSvcImmediate;
using liba32android::compat::kA32PthreadAttrInitSvcImmediate;
using liba32android::compat::kA32PthreadAttrSetdetachstateSvcImmediate;
using liba32android::compat::kA32PthreadAttrSetstacksizeSvcImmediate;
using liba32android::compat::kA32AndroidEtimedout;
using liba32android::compat::kA32PthreadCondBroadcastSvcImmediate;
using liba32android::compat::kA32PthreadCondDestroySvcImmediate;
using liba32android::compat::kA32PthreadCondInitSvcImmediate;
using liba32android::compat::kA32PthreadCondSignalSvcImmediate;
using liba32android::compat::kA32PthreadCondTimedwaitSvcImmediate;
using liba32android::compat::kA32PthreadCondWaitSvcImmediate;
using liba32android::compat::kA32PthreadCreateDetached;
using liba32android::compat::kA32PthreadCreateJoinable;
using liba32android::compat::kA32PthreadCreateSvcImmediate;
using liba32android::compat::kA32PthreadDetachSvcImmediate;
using liba32android::compat::kA32PthreadEqualSvcImmediate;
using liba32android::compat::kA32PthreadExitSvcImmediate;
using liba32android::compat::kA32PthreadJoinSvcImmediate;
using liba32android::compat::kA32PthreadSelfSvcImmediate;
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
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::A32LogicalThreadId;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::execute_a32_with_services;
using liba32android::runtime::make_a32_service_resume_context;

constexpr std::uint32_t kMaxFixtureNameBytes = 128U;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20U;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20U;
constexpr std::size_t kInstructionBudget = 256U;

class FixtureClock final : public A32PthreadClock {
public:
    std::optional<std::int64_t> realtime_ns{0};

    std::optional<std::int64_t> now_ns(
        A32PthreadClockId clock_id) const noexcept override {
        if (clock_id != A32PthreadClockId::Realtime) {
            return std::nullopt;
        }
        return realtime_ns;
    }
};

bool write_timespec(
    MappedGuestMemory& memory,
    std::uint32_t address,
    std::int32_t seconds,
    std::int32_t nanoseconds) {
    const std::uint32_t sec = std::bit_cast<std::uint32_t>(seconds);
    const std::uint32_t nsec = std::bit_cast<std::uint32_t>(nanoseconds);
    const std::array<std::uint8_t, 8> bytes{{
        static_cast<std::uint8_t>(sec),
        static_cast<std::uint8_t>(sec >> 8U),
        static_cast<std::uint8_t>(sec >> 16U),
        static_cast<std::uint8_t>(sec >> 24U),
        static_cast<std::uint8_t>(nsec),
        static_cast<std::uint8_t>(nsec >> 8U),
        static_cast<std::uint8_t>(nsec >> 16U),
        static_cast<std::uint8_t>(nsec >> 24U),
    }};
    return memory.write(address, bytes);
}

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 256,
        .max_hash_buckets = 256,
        .max_gnu_bloom_words = 128,
        .max_scope_objects = 8,
        .max_name_bytes = kMaxFixtureNameBytes,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 32U;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("pthread lifecycle symbol lookup failed for ") +
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
    if (page_count == 0U ||
        length > std::numeric_limits<std::uint32_t>::max()) {
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

std::optional<std::uint32_t> root_function(
    MappedGuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::string_view name) {
    const auto lookup =
        lookup_elf32_graph_symbol(memory, graph, 0, name, symbol_options());
    if (!lookup || lookup.symbol.object_index != 0 ||
        lookup.symbol.symbol.symbol.type != 2U) {
        if (!lookup) std::cerr << lookup_error(name, lookup) << '\n';
        return std::nullopt;
    }
    return lookup.symbol.symbol.guest_value;
}

std::optional<A32ServiceDispatchResult> run_wrapper(
    MappedGuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::string_view name,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::uint32_t r0 = 0U,
    std::uint32_t r1 = 0U,
    std::uint32_t r2 = 0U,
    std::uint32_t r3 = 0U) {
    const auto function = root_function(memory, graph, name);
    if (!function.has_value()) return std::nullopt;

    const bool thumb = (*function & 1U) != 0U;
    ExecutionRequest request{};
    request.instruction_set =
        thumb ? InstructionSet::Thumb : InstructionSet::Arm;
    request.entry_pc = *function & ~1U;
    request.regs[0] = r0;
    request.regs[1] = r1;
    request.regs[2] = r2;
    request.regs[3] = r3;
    request.regs[13] = stack_top;
    request.regs[14] = stop_pc | (thumb ? 1U : 0U);
    request.instruction_count = kInstructionBudget;
    request.stop_pc = stop_pc;
    return execute_a32_with_services(memory, request, registry, 1U);
}

std::uint32_t read_u32(
    const MappedGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return 0xffffffffU;
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

const A32PthreadThreadState* find_thread(
    std::span<const A32PthreadThreadState> threads,
    std::uint32_t pthread_id) {
    for (const auto& thread : threads) {
        if (thread.phase != A32PthreadThreadPhase::Free &&
            thread.pthread_id == pthread_id) {
            return &thread;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail("expected paths to pthread lifecycle consumer and partial libc.so shim");
    }

    const auto consumer_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const auto shim_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (consumer_image.empty() || shim_image.empty()) {
        return fail("pthread lifecycle ARM32 fixture is missing or empty");
    }

    MappedGuestMemory memory;
    const std::array<Elf32DependencyCatalogEntry, 0> app_entries{};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};
    const std::array<Elf32DependencyCatalogEntry, 1> platform_entries{{
        make_a32_libc_memory_string_shim_catalog_entry(shim_image),
    }};
    const std::array<A32AndroidNamespaceBinding, 1> namespace_bindings{{
        {"pthread-lifecycle-consumer", "app"},
    }};
    const std::array<std::string_view, 1> shared_libs{{
        kA32LibcMemoryStringShimSoname,
    }};
    const std::array<A32AndroidNamespaceLink, 1> namespace_links{{
        {"app", "platform", false, std::span{shared_libs}},
    }};
    A32AndroidNamespaceAccessPolicy namespace_policy{
        std::span{namespace_bindings},
        std::span{namespace_links},
        "platform",
    };
    A32AndroidPlatformCatalogProvider platform_provider{
        std::span{platform_entries}, namespace_policy};
    const std::array<Elf32DependencyProvider*, 2> providers{{
        &app_provider,
        &platform_provider,
    }};
    Elf32DependencyProviderChain provider_chain{std::span{providers}};

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 8U;
    load_options.max_depth = 8U;
    load_options.max_dependency_occurrences = 8U;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxTotalImageBytes;
    load_options.max_string_bytes = kMaxFixtureNameBytes;

    const auto graph_result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "pthread-lifecycle-consumer",
            .image = consumer_image,
        },
        provider_chain,
        load_options);
    if (!graph_result || graph_result.graph.objects.size() != 2U) {
        return fail("pthread lifecycle dependency graph load failed");
    }
    if (graph_result.graph.objects[1].identity !=
        kA32LibcMemoryStringShimIdentity) {
        return fail("pthread lifecycle consumer did not bind partial libc shim");
    }

    constexpr std::array<std::string_view, 18> shim_names{{
        "pthread_attr_init",
        "pthread_attr_destroy",
        "pthread_attr_getdetachstate",
        "pthread_attr_setdetachstate",
        "pthread_attr_getstacksize",
        "pthread_attr_setstacksize",
        "pthread_create",
        "pthread_self",
        "pthread_equal",
        "pthread_exit",
        "pthread_join",
        "pthread_detach",
        "pthread_cond_init",
        "pthread_cond_destroy",
        "pthread_cond_wait",
        "pthread_cond_timedwait",
        "pthread_cond_signal",
        "pthread_cond_broadcast",
    }};
    std::array<std::uint32_t, shim_names.size()> targets{};
    for (std::size_t i = 0; i < shim_names.size(); ++i) {
        const auto lookup = lookup_elf32_graph_symbol(
            memory, graph_result.graph, 0, shim_names[i], symbol_options());
        if (!lookup) return fail(lookup_error(shim_names[i], lookup));
        if (lookup.symbol.object_index != 1U ||
            lookup.symbol.symbol.symbol.type != 2U) {
            return fail("pthread lifecycle import did not resolve to shim");
        }
        targets[i] = lookup.symbol.symbol.guest_value;
    }

    const auto relocated = apply_elf32_combined_relocations(
        memory,
        graph_result.graph,
        0,
        relocation_options());
    if (!relocated) {
        return fail("pthread lifecycle consumer relocation failed");
    }
    for (const std::uint32_t target : targets) {
        bool found = false;
        for (const auto& write : relocated.application.writes) {
            if (write.type == kRArmJumpSlot &&
                write.final_word == target) {
                found = true;
                break;
            }
        }
        if (!found) {
            return fail("pthread lifecycle consumer missing JUMP_SLOT target");
        }
    }

    const auto data = find_unmapped_region(memory, 0x70000000U, 1U);
    const auto call_stack = find_unmapped_region(memory, 0x71000000U, 4U);
    const auto thread_stacks = find_unmapped_region(memory, 0x72000000U, 8U);
    const auto stop = find_unmapped_region(memory, 0x73000000U, 1U);
    if (!data.has_value() || !call_stack.has_value() ||
        !thread_stacks.has_value() || !stop.has_value()) {
        return fail("could not reserve pthread lifecycle harness regions");
    }
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*data, memory.page_size(), rw) ||
        !memory.map(*call_stack, memory.page_size() * 4U, rw) ||
        !memory.map(*thread_stacks, memory.page_size() * 8U, rw)) {
        return fail("could not map pthread lifecycle harness regions");
    }
    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(*call_stack) +
            memory.page_size() * 4U - 16U) & ~7U;

    const auto exit_lookup = lookup_elf32_graph_symbol(
        memory,
        graph_result.graph,
        0,
        "pthread_exit",
        symbol_options());
    if (!exit_lookup || exit_lookup.symbol.object_index != 1U) {
        return fail("could not resolve pthread_exit trampoline");
    }

    FixtureClock clock{};
    std::array<A32PthreadMutexState, 4> mutexes{};
    std::array<A32SemaphoreState, 2> semaphores{};
    std::array<A32PthreadWaiter, 8> waiters{};
    std::array<A32PthreadKeyState, 2> keys{};
    std::array<A32PthreadTlsValue, 4> tls_values{};
    A32PthreadSyncService sync{
        std::span{mutexes},
        std::span{semaphores},
        std::span{waiters},
        std::span{keys},
        std::span{tls_values},
        &clock,
    };

    std::array<A32PthreadAttrState, 4> attrs{};
    std::array<A32PthreadThreadState, 4> threads{};
    A32PthreadLifecycleService lifecycle{
        A32PthreadLifecycleOptions{
            .stack_arena_base = *thread_stacks,
            .stack_arena_size =
                static_cast<std::uint32_t>(memory.page_size() * 8U),
            .page_size = static_cast<std::uint32_t>(memory.page_size()),
            .default_stack_size =
                static_cast<std::uint32_t>(memory.page_size() * 2U),
            .exit_trampoline = exit_lookup.symbol.symbol.guest_value,
            .thread_instruction_budget = kInstructionBudget,
            .first_thread_id = 2U,
        },
        std::span{attrs},
        std::span{threads},
    };
    const auto root = A32LogicalThreadId::from_raw(1U);
    if (!root.has_value() ||
        !lifecycle.register_initial_thread(*root) ||
        !lifecycle.set_current_thread_id(*root) ||
        !sync.set_current_thread_id(*root)) {
        return fail("could not register initial logical pthread");
    }

    const std::array<A32HostServiceRegistryEntry, 18> services{{
        {kA32PthreadAttrInitSvcImmediate, &lifecycle},
        {kA32PthreadAttrDestroySvcImmediate, &lifecycle},
        {kA32PthreadAttrGetdetachstateSvcImmediate, &lifecycle},
        {kA32PthreadAttrSetdetachstateSvcImmediate, &lifecycle},
        {kA32PthreadAttrGetstacksizeSvcImmediate, &lifecycle},
        {kA32PthreadAttrSetstacksizeSvcImmediate, &lifecycle},
        {kA32PthreadCreateSvcImmediate, &lifecycle},
        {kA32PthreadSelfSvcImmediate, &lifecycle},
        {kA32PthreadEqualSvcImmediate, &lifecycle},
        {kA32PthreadExitSvcImmediate, &lifecycle},
        {kA32PthreadJoinSvcImmediate, &lifecycle},
        {kA32PthreadDetachSvcImmediate, &lifecycle},
        {kA32PthreadCondInitSvcImmediate, &sync},
        {kA32PthreadCondDestroySvcImmediate, &sync},
        {kA32PthreadCondWaitSvcImmediate, &sync},
        {kA32PthreadCondTimedwaitSvcImmediate, &sync},
        {kA32PthreadCondSignalSvcImmediate, &sync},
        {kA32PthreadCondBroadcastSvcImmediate, &sync},
    }};
    A32HostServiceRegistry registry{std::span{services}};

    std::size_t wrapper_calls = 0U;
    std::size_t thread_exits = 0U;

    auto result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_self",
        registry,
        stack_top,
        *stop);
    if (!result || !*result || result->regs[0] != 1U) {
        return fail("real pthread_self wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_equal",
        registry,
        stack_top,
        *stop,
        1U,
        1U);
    if (!result || !*result || result->regs[0] != 1U) {
        return fail("real pthread_equal equal case failed");
    }
    ++wrapper_calls;
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_equal",
        registry,
        stack_top,
        *stop,
        1U,
        2U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_equal distinct case failed");
    }
    ++wrapper_calls;

    const std::uint32_t attr_address = *data + 0x100U;
    const std::uint32_t detach_out = *data + 0x140U;
    const std::uint32_t stack_size_out = *data + 0x144U;
    const std::uint32_t thread_out = *data + 0x180U;
    const std::uint32_t join_result_out = *data + 0x188U;
    const std::uint32_t cond_address = *data + 0x1a0U;
    const std::uint32_t mutex_address = *data + 0x1c0U;
    const std::uint32_t timespec_address = *data + 0x1e0U;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_init",
        registry,
        stack_top,
        *stop,
        attr_address);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_attr_init wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_setdetachstate",
        registry,
        stack_top,
        *stop,
        attr_address,
        kA32PthreadCreateDetached);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_attr_setdetachstate wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_getdetachstate",
        registry,
        stack_top,
        *stop,
        attr_address,
        detach_out);
    if (!result || !*result || result->regs[0] != 0U ||
        read_u32(memory, detach_out) != kA32PthreadCreateDetached) {
        return fail("real pthread_attr_getdetachstate wrapper failed");
    }
    ++wrapper_calls;

    const std::uint32_t requested_stack =
        static_cast<std::uint32_t>(memory.page_size() * 2U);
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_setstacksize",
        registry,
        stack_top,
        *stop,
        attr_address,
        requested_stack);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_attr_setstacksize wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_getstacksize",
        registry,
        stack_top,
        *stop,
        attr_address,
        stack_size_out);
    if (!result || !*result || result->regs[0] != 0U ||
        read_u32(memory, stack_size_out) != requested_stack) {
        return fail("real pthread_attr_getstacksize wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_setdetachstate",
        registry,
        stack_top,
        *stop,
        attr_address,
        kA32PthreadCreateJoinable);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_attr_setdetachstate joinable restore failed");
    }
    ++wrapper_calls;

    const auto start_return =
        root_function(memory, graph_result.graph, "fixture_pthread_start_return");
    const auto start_exit =
        root_function(memory, graph_result.graph, "fixture_pthread_start_exit");
    if (!start_return.has_value() || !start_exit.has_value()) {
        return fail("could not resolve pthread lifecycle start routines");
    }

    constexpr std::uint32_t kReturnValue = 0x12345678U;
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_create",
        registry,
        stack_top,
        *stop,
        thread_out,
        attr_address,
        *start_return,
        kReturnValue);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_create returning-thread wrapper failed");
    }
    ++wrapper_calls;
    const std::uint32_t first_id = read_u32(memory, thread_out);
    auto created = lifecycle.pop_created_thread();
    if (!created.has_value() ||
        created->pthread_id != first_id ||
        created->detached) {
        return fail("created returning thread context was incorrect");
    }

    auto join_wait = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_join",
        registry,
        stack_top,
        *stop,
        first_id,
        join_result_out);
    if (!join_wait.has_value() ||
        !join_wait->service_suspended ||
        !join_wait->suspended_svc_immediate.has_value() ||
        *join_wait->suspended_svc_immediate != kA32PthreadJoinSvcImmediate) {
        return fail("real pthread_join did not suspend on live target");
    }
    ++wrapper_calls;

    if (!lifecycle.set_current_thread_context(created->context)) {
        return fail("could not select joined target context");
    }
    const auto returned = execute_a32_with_services(
        memory,
        created->context.request,
        registry,
        2U);
    const auto* first_state = find_thread(std::span{threads}, first_id);
    if (!returned || !returned.service_suspended ||
        !returned.suspended_svc_immediate.has_value() ||
        *returned.suspended_svc_immediate != kA32PthreadExitSvcImmediate ||
        first_state == nullptr ||
        first_state->phase != A32PthreadThreadPhase::Exited ||
        first_state->return_value != kReturnValue ||
        read_u32(memory, join_result_out) != kReturnValue) {
        return fail("returning ARM32 start routine did not complete blocked join");
    }
    ++thread_exits;

    const auto wake = lifecycle.pop_ready_join();
    if (!wake.has_value() ||
        wake->thread_id != 1U ||
        wake->target_thread_id != first_id ||
        find_thread(std::span{threads}, first_id) != nullptr) {
        return fail("real pthread_join wake/reclamation was incorrect");
    }
    const auto root_joiner = A32LogicalThreadId::from_raw(wake->thread_id);
    const auto join_resume = root_joiner.has_value()
        ? make_a32_service_resume_context(
              *root_joiner, *join_wait, kInstructionBudget, *stop)
        : std::nullopt;
    if (!join_resume.has_value() ||
        !lifecycle.set_current_thread_context(*join_resume)) {
        return fail("could not build real pthread_join continuation");
    }
    const auto join_complete = execute_a32_with_services(
        memory,
        join_resume->request,
        registry,
        1U);
    if (!join_complete || !join_complete.stop_pc_reached ||
        join_complete.regs[0] != 0U) {
        return fail("real pthread_join continuation did not return normally");
    }

    lifecycle.set_current_thread_id(1U);
    constexpr std::uint32_t kExplicitExitValue = 0xcafebabeU;
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_create",
        registry,
        stack_top,
        *stop,
        thread_out + 4U,
        attr_address,
        *start_exit,
        kExplicitExitValue);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_create explicit-exit wrapper failed");
    }
    ++wrapper_calls;
    const std::uint32_t second_id = read_u32(memory, thread_out + 4U);
    created = lifecycle.pop_created_thread();
    if (!created.has_value() ||
        created->pthread_id != second_id) {
        return fail("created explicit-exit context was incorrect");
    }

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_detach",
        registry,
        stack_top,
        *stop,
        second_id);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_detach wrapper failed");
    }
    ++wrapper_calls;

    if (!lifecycle.set_current_thread_context(created->context)) {
        return fail("could not select detached explicit-exit context");
    }
    const auto exited = execute_a32_with_services(
        memory,
        created->context.request,
        registry,
        2U);
    if (!exited || !exited.service_suspended ||
        find_thread(std::span{threads}, second_id) != nullptr) {
        return fail("detached explicit pthread_exit did not reclaim target");
    }
    ++thread_exits;


    sync.set_current_thread_id(1U);
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_init",
        registry,
        stack_top,
        *stop,
        cond_address,
        0U);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_cond_init wrapper failed");
    }
    ++wrapper_calls;

    std::array<std::uint32_t, 16> sync_regs{};
    std::uint32_t sync_cpsr{};
    sync_regs[0] = mutex_address;
    if (sync.handle(
            memory,
            liba32android::compat::kA32PthreadMutexLockSvcImmediate,
            sync_regs,
            sync_cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not lock mutex for real pthread_cond_wait");
    }

    auto cond_wait = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_wait",
        registry,
        stack_top,
        *stop,
        cond_address,
        mutex_address);
    if (!cond_wait.has_value() ||
        !cond_wait->service_suspended ||
        !cond_wait->suspended_svc_immediate.has_value() ||
        *cond_wait->suspended_svc_immediate !=
            kA32PthreadCondWaitSvcImmediate) {
        return fail("real pthread_cond_wait did not suspend");
    }
    ++wrapper_calls;

    sync.set_current_thread_id(2U);
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_signal",
        registry,
        stack_top,
        *stop,
        cond_address);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_cond_signal wrapper failed");
    }
    ++wrapper_calls;

    auto cond_wake = sync.pop_ready();
    if (!cond_wake.has_value() ||
        cond_wake->kind != A32PthreadWaitKind::Condition ||
        cond_wake->thread_id != 1U ||
        cond_wake->result_value != 0U) {
        return fail("real pthread_cond_signal did not publish waiter");
    }
    auto cond_resume = make_a32_service_resume_context(
        *root, *cond_wait, kInstructionBudget, *stop);
    if (!cond_resume.has_value() ||
        !sync.set_current_thread_context(*cond_resume)) {
        return fail("could not resume real pthread_cond_wait");
    }
    cond_resume->request.regs[0] = cond_wake->result_value;
    const auto cond_complete = execute_a32_with_services(
        memory,
        cond_resume->request,
        registry,
        1U);
    if (!cond_complete || !cond_complete.stop_pc_reached ||
        cond_complete.regs[0] != 0U) {
        return fail("real pthread_cond_wait continuation failed");
    }

    sync_regs = {};
    sync_regs[0] = mutex_address;
    if (sync.handle(
            memory,
            liba32android::compat::kA32PthreadMutexUnlockSvcImmediate,
            sync_regs,
            sync_cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not unlock mutex after real pthread_cond_wait");
    }

    sync.set_current_thread_id(1U);
    sync_regs = {};
    sync_regs[0] = mutex_address;
    if (sync.handle(
            memory,
            liba32android::compat::kA32PthreadMutexLockSvcImmediate,
            sync_regs,
            sync_cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not lock mutex for real timed wait");
    }
    clock.realtime_ns = 4'000'000'000LL;
    if (!write_timespec(memory, timespec_address, 5, 0)) {
        return fail("could not write real timed-wait timespec");
    }
    auto timed_wait = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_timedwait",
        registry,
        stack_top,
        *stop,
        cond_address,
        mutex_address,
        timespec_address);
    if (!timed_wait.has_value() ||
        !timed_wait->service_suspended) {
        return fail("real pthread_cond_timedwait did not suspend");
    }
    ++wrapper_calls;

    clock.realtime_ns = 5'000'000'000LL;
    if (!sync.poll_condition_timeouts()) {
        return fail("real timed wait deadline poll failed");
    }
    cond_wake = sync.pop_ready();
    if (!cond_wake.has_value() ||
        cond_wake->kind != A32PthreadWaitKind::Condition ||
        static_cast<std::int32_t>(cond_wake->result_value) !=
            kA32AndroidEtimedout) {
        return fail("real timed wait did not publish ETIMEDOUT");
    }
    cond_resume = make_a32_service_resume_context(
        *root, *timed_wait, kInstructionBudget, *stop);
    if (!cond_resume.has_value() ||
        !sync.set_current_thread_context(*cond_resume)) {
        return fail("could not resume real timed wait");
    }
    cond_resume->request.regs[0] = cond_wake->result_value;
    const auto timed_complete = execute_a32_with_services(
        memory,
        cond_resume->request,
        registry,
        1U);
    if (!timed_complete || !timed_complete.stop_pc_reached ||
        static_cast<std::int32_t>(timed_complete.regs[0]) !=
            kA32AndroidEtimedout) {
        return fail("real timed wait continuation returned wrong result");
    }

    sync_regs = {};
    sync_regs[0] = mutex_address;
    if (sync.handle(
            memory,
            liba32android::compat::kA32PthreadMutexUnlockSvcImmediate,
            sync_regs,
            sync_cpsr) != A32HostServiceDisposition::Handled) {
        return fail("could not unlock mutex after real timed wait");
    }

    sync.set_current_thread_id(2U);
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_broadcast",
        registry,
        stack_top,
        *stop,
        cond_address);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_cond_broadcast wrapper failed");
    }
    ++wrapper_calls;

    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_cond_destroy",
        registry,
        stack_top,
        *stop,
        cond_address);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_cond_destroy wrapper failed");
    }
    ++wrapper_calls;

    lifecycle.set_current_thread_id(1U);
    result = run_wrapper(
        memory,
        graph_result.graph,
        "fixture_pthread_attr_destroy",
        registry,
        stack_top,
        *stop,
        attr_address);
    if (!result || !*result || result->regs[0] != 0U) {
        return fail("real pthread_attr_destroy wrapper failed");
    }
    ++wrapper_calls;

    std::cout
        << "fixture.pthread_lifecycle.object_count="
        << graph_result.graph.objects.size() << '\n'
        << "fixture.pthread_lifecycle.required_jump_slots="
        << targets.size() << '\n'
        << "fixture.pthread_lifecycle.wrapper_calls="
        << wrapper_calls << '\n'
        << "fixture.pthread_lifecycle.thread_exits="
        << thread_exits << '\n'
        << "fixture.pthread_lifecycle.status=PASS\n";
    return 0;
}
