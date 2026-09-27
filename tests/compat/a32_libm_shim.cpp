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
#include "compat/a32_libm.h"
#include "cpu/a32_cpu.h"
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
using liba32android::compat::A32LibmService;
using liba32android::compat::kA32LibmAcosSvcImmediate;
using liba32android::compat::kA32LibmAsinSvcImmediate;
using liba32android::compat::kA32LibmAtan2SvcImmediate;
using liba32android::compat::kA32LibmCosSvcImmediate;
using liba32android::compat::kA32LibmCosfSvcImmediate;
using liba32android::compat::kA32LibmExpSvcImmediate;
using liba32android::compat::kA32LibmFloorSvcImmediate;
using liba32android::compat::kA32LibmFrexpSvcImmediate;
using liba32android::compat::kA32LibmLdexpSvcImmediate;
using liba32android::compat::kA32LibmLogSvcImmediate;
using liba32android::compat::kA32LibmLog10SvcImmediate;
using liba32android::compat::kA32LibmLog10fSvcImmediate;
using liba32android::compat::kA32LibmPowSvcImmediate;
using liba32android::compat::kA32LibmPowfSvcImmediate;
using liba32android::compat::kA32LibmShimIdentity;
using liba32android::compat::kA32LibmShimSoname;
using liba32android::compat::kA32LibmSinSvcImmediate;
using liba32android::compat::kA32LibmSinfSvcImmediate;
using liba32android::compat::kA32LibmTanSvcImmediate;
using liba32android::compat::make_a32_libm_shim_catalog_entry;
using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderChain;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::execute_a32_with_services;

constexpr std::uint32_t kMaxFixtureNameBytes = 128U;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::size_t kInstructionBudget = 256U;
constexpr std::size_t kStackPages = 4U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 512U,
        .max_hash_buckets = 512U,
        .max_gnu_bloom_words = 128U,
        .max_scope_objects = 8U,
        .max_name_bytes = kMaxFixtureNameBytes,
        .max_version_records = 256U,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 64U;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("libm shim symbol lookup failed for ") +
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

std::array<std::uint32_t, 2> double_words(double value) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return {
        static_cast<std::uint32_t>(bits),
        static_cast<std::uint32_t>(bits >> 32U),
    };
}

double result_double(const A32ServiceDispatchResult& result) {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(result.regs[0]) |
        (static_cast<std::uint64_t>(result.regs[1]) << 32U);
    return std::bit_cast<double>(bits);
}

std::int32_t read_i32(
    const MappedGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return -9999;
    }
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return std::bit_cast<std::int32_t>(bits);
}

std::optional<A32ServiceDispatchResult> run_wrapper(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    std::string_view name,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::uint32_t r0,
    std::uint32_t r1 = 0U,
    std::uint32_t r2 = 0U,
    std::uint32_t r3 = 0U) {
    const auto lookup = lookup_elf32_graph_symbol(
        memory, graph, 0U, name, symbol_options());
    if (!lookup) {
        std::cerr << lookup_error(name, lookup) << '\n';
        return std::nullopt;
    }
    if (lookup.symbol.object_index != 0U ||
        lookup.symbol.symbol.symbol.type != 2U ||
        lookup.symbol.symbol.symbol.size == 0U) {
        std::cerr << "wrapper symbol was not a non-empty root STT_FUNC: "
                  << name << '\n';
        return std::nullopt;
    }
    const auto& symbol = lookup.symbol.symbol;
    const bool thumb = (symbol.symbol.value & 1U) != 0U;
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
    return execute_a32_with_services(memory, request, registry, 1U);
}

bool exact_double_call(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::string_view wrapper,
    double input,
    double expected) {
    const auto words = double_words(input);
    const auto result = run_wrapper(
        memory, graph, wrapper, registry, stack_top, stop_pc,
        words[0], words[1]);
    return result.has_value() && *result &&
           result->services_handled == 1U &&
           result_double(*result) == expected;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail("expected ARM32 libm consumer and libm.so shim paths");
    }

    const std::vector<std::uint8_t> consumer_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> shim_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (consumer_image.empty() || shim_image.empty()) {
        return fail("generated ARM32 libm fixture is missing or empty");
    }

    MappedGuestMemory memory;
    const std::array<Elf32DependencyCatalogEntry, 0> app_entries{};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};

    const std::array<A32AndroidNamespaceBinding, 1> bindings{{
        {"libm-consumer", "app"},
    }};
    const std::array<std::string_view, 1> platform_shared_libs{{
        kA32LibmShimSoname,
    }};
    const std::array<A32AndroidNamespaceLink, 1> links{{
        {"app", "platform", false, std::span{platform_shared_libs}},
    }};
    A32AndroidNamespaceAccessPolicy policy{
        std::span{bindings}, std::span{links}, "platform"};
    const std::array<Elf32DependencyCatalogEntry, 1> platform_entries{{
        make_a32_libm_shim_catalog_entry(
            std::span<const std::uint8_t>{shim_image}),
    }};
    A32AndroidPlatformCatalogProvider platform_provider{
        std::span{platform_entries}, policy};
    const std::array<Elf32DependencyProvider*, 2> providers{{
        &app_provider, &platform_provider,
    }};
    Elf32DependencyProviderChain provider_chain{std::span{providers}};

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 4U;
    load_options.max_depth = 4U;
    load_options.max_dependency_occurrences = 4U;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxTotalImageBytes;
    load_options.max_string_bytes = kMaxFixtureNameBytes;

    const auto loaded = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "libm-consumer",
            .image = consumer_image,
        },
        provider_chain,
        load_options);
    if (!loaded || loaded.graph.objects.size() != 2U) {
        return fail("libm namespace-gated dependency graph did not load");
    }
    if (loaded.graph.objects[1].identity != kA32LibmShimIdentity ||
        loaded.graph.objects[0].dependencies.size() != 1U ||
        loaded.graph.objects[0].dependencies[0].requested_name !=
            kA32LibmShimSoname) {
        return fail("libm shim dependency/provider metadata was incorrect");
    }

    constexpr std::array<std::string_view, 17> names{{
        "acos", "asin", "atan2", "cos", "cosf", "exp", "floor", "frexp",
        "ldexp", "log", "log10", "log10f", "pow", "powf", "sin", "sinf",
        "tan",
    }};
    for (const auto name : names) {
        const auto symbol = lookup_elf32_graph_symbol(
            memory, loaded.graph, 0U, name, symbol_options());
        if (!symbol) {
            return fail(lookup_error(name, symbol));
        }
        if (symbol.symbol.object_index != 1U) {
            return fail("libm symbol resolved from wrong graph object");
        }
    }

    const auto relocated = apply_elf32_combined_relocations(
        memory, loaded.graph, 0U, relocation_options());
    if (!relocated || relocated.application.writes.size() != names.size()) {
        return fail("libm consumer did not receive seventeen eager relocations");
    }

    const auto data = find_unmapped_region(memory, 0x70000000U, 1U);
    const auto stack = find_unmapped_region(memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(memory, 0x72000000U, 1U);
    if (!data.has_value() || !stack.has_value() || !stop.has_value()) {
        return fail("could not reserve libm execution harness regions");
    }
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*data, memory.page_size(), rw) ||
        !memory.map(*stack, memory.page_size() * kStackPages, rw)) {
        return fail("could not map libm data/stack");
    }
    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("libm stack top overflowed guest address space");
    }
    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(stack_top64) & ~7U;

    A32LibmService service;
    const std::array<A32HostServiceRegistryEntry, 17> services{{
        {kA32LibmAcosSvcImmediate, &service},
        {kA32LibmAsinSvcImmediate, &service},
        {kA32LibmAtan2SvcImmediate, &service},
        {kA32LibmCosSvcImmediate, &service},
        {kA32LibmCosfSvcImmediate, &service},
        {kA32LibmExpSvcImmediate, &service},
        {kA32LibmFloorSvcImmediate, &service},
        {kA32LibmFrexpSvcImmediate, &service},
        {kA32LibmLdexpSvcImmediate, &service},
        {kA32LibmLogSvcImmediate, &service},
        {kA32LibmLog10SvcImmediate, &service},
        {kA32LibmLog10fSvcImmediate, &service},
        {kA32LibmPowSvcImmediate, &service},
        {kA32LibmPowfSvcImmediate, &service},
        {kA32LibmSinSvcImmediate, &service},
        {kA32LibmSinfSvcImmediate, &service},
        {kA32LibmTanSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{services}};
    std::size_t completed_calls = 0U;

    const struct {
        const char* wrapper;
        double input;
        double expected;
    } unary_cases[] = {
        {"fixture_acos", 1.0, 0.0},
        {"fixture_asin", 0.0, 0.0},
        {"fixture_cos", 0.0, 1.0},
        {"fixture_exp", 0.0, 1.0},
        {"fixture_floor", 1.75, 1.0},
        {"fixture_log", 1.0, 0.0},
        {"fixture_log10", 1.0, 0.0},
        {"fixture_sin", 0.0, 0.0},
        {"fixture_tan", 0.0, 0.0},
    };
    for (const auto& test_case : unary_cases) {
        if (!exact_double_call(
                memory, loaded.graph, registry, stack_top, *stop,
                test_case.wrapper, test_case.input, test_case.expected)) {
            return fail(std::string("real libm unary wrapper failed: ") +
                        test_case.wrapper);
        }
        ++completed_calls;
    }

    auto first = double_words(0.0);
    auto second = double_words(1.0);
    auto result = run_wrapper(
        memory, loaded.graph, "fixture_atan2", registry, stack_top, *stop,
        first[0], first[1], second[0], second[1]);
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 0.0) {
        return fail("real libm atan2 wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, loaded.graph, "fixture_cosf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(0.0f));
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 1.0f) {
        return fail("real libm cosf wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t exponent_address = *data + 0x100U;
    first = double_words(8.0);
    result = run_wrapper(
        memory, loaded.graph, "fixture_frexp", registry, stack_top, *stop,
        first[0], first[1], exponent_address);
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 0.5 ||
        read_i32(memory, exponent_address) != 4) {
        return fail("real libm frexp wrapper failed");
    }
    ++completed_calls;

    first = double_words(0.5);
    result = run_wrapper(
        memory, loaded.graph, "fixture_ldexp", registry, stack_top, *stop,
        first[0], first[1],
        std::bit_cast<std::uint32_t>(std::int32_t{4}));
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 8.0) {
        return fail("real libm ldexp wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, loaded.graph, "fixture_log10f", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(1.0f));
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 0.0f) {
        return fail("real libm log10f wrapper failed");
    }
    ++completed_calls;

    first = double_words(2.0);
    second = double_words(3.0);
    result = run_wrapper(
        memory, loaded.graph, "fixture_pow", registry, stack_top, *stop,
        first[0], first[1], second[0], second[1]);
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 8.0) {
        return fail("real libm pow wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, loaded.graph, "fixture_powf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(2.0f),
        std::bit_cast<std::uint32_t>(3.0f));
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 8.0f) {
        return fail("real libm powf wrapper failed");
    }
    ++completed_calls;

    result = run_wrapper(
        memory, loaded.graph, "fixture_sinf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(0.0f));
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 0.0f) {
        return fail("real libm sinf wrapper failed");
    }
    ++completed_calls;

    if (completed_calls != names.size()) {
        return fail("libm wrapper execution count mismatch");
    }

    std::cout
        << "fixture.libm.object_count=" << loaded.graph.objects.size() << '\n'
        << "fixture.libm.needed=" << kA32LibmShimSoname << '\n'
        << "fixture.libm.namespace_access=linked\n"
        << "fixture.libm.required_jump_slots="
        << relocated.application.writes.size() << '\n'
        << "fixture.libm.completed_service_calls=" << completed_calls << '\n'
        << "fixture.libm.status=PASS\n";
    return 0;
}
