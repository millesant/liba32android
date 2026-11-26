#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
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
using liba32android::compat::kA32LibmAcosfSvcImmediate;
using liba32android::compat::kA32LibmAtanSvcImmediate;
using liba32android::compat::kA32LibmAtan2fSvcImmediate;
using liba32android::compat::kA32LibmAtanfSvcImmediate;
using liba32android::compat::kA32LibmCbrtSvcImmediate;
using liba32android::compat::kA32LibmCbrtfSvcImmediate;
using liba32android::compat::kA32LibmCeilSvcImmediate;
using liba32android::compat::kA32LibmCeilfSvcImmediate;
using liba32android::compat::kA32LibmCoshSvcImmediate;
using liba32android::compat::kA32LibmExp2SvcImmediate;
using liba32android::compat::kA32LibmExp2fSvcImmediate;
using liba32android::compat::kA32LibmExpfSvcImmediate;
using liba32android::compat::kA32LibmExpm1SvcImmediate;
using liba32android::compat::kA32LibmFabsSvcImmediate;
using liba32android::compat::kA32LibmFloorfSvcImmediate;
using liba32android::compat::kA32LibmFmaxSvcImmediate;
using liba32android::compat::kA32LibmFmaxfSvcImmediate;
using liba32android::compat::kA32LibmFminfSvcImmediate;
using liba32android::compat::kA32LibmFmodSvcImmediate;
using liba32android::compat::kA32LibmFmodfSvcImmediate;
using liba32android::compat::kA32LibmFrexpfSvcImmediate;
using liba32android::compat::kA32LibmHypotSvcImmediate;
using liba32android::compat::kA32LibmHypotfSvcImmediate;
using liba32android::compat::kA32LibmLdexpfSvcImmediate;
using liba32android::compat::kA32LibmLlrintSvcImmediate;
using liba32android::compat::kA32LibmLlrintfSvcImmediate;
using liba32android::compat::kA32LibmLlroundSvcImmediate;
using liba32android::compat::kA32LibmLlroundfSvcImmediate;
using liba32android::compat::kA32LibmLog1pSvcImmediate;
using liba32android::compat::kA32LibmLogfSvcImmediate;
using liba32android::compat::kA32LibmLrintSvcImmediate;
using liba32android::compat::kA32LibmLrintfSvcImmediate;
using liba32android::compat::kA32LibmLroundSvcImmediate;
using liba32android::compat::kA32LibmLroundfSvcImmediate;
using liba32android::compat::kA32LibmModfSvcImmediate;
using liba32android::compat::kA32LibmModffSvcImmediate;
using liba32android::compat::kA32LibmNanfSvcImmediate;
using liba32android::compat::kA32LibmRintSvcImmediate;
using liba32android::compat::kA32LibmRintfSvcImmediate;
using liba32android::compat::kA32LibmRoundSvcImmediate;
using liba32android::compat::kA32LibmRoundfSvcImmediate;
using liba32android::compat::kA32LibmScalbnSvcImmediate;
using liba32android::compat::kA32LibmSincosSvcImmediate;
using liba32android::compat::kA32LibmSincosfSvcImmediate;
using liba32android::compat::kA32LibmSinhSvcImmediate;
using liba32android::compat::kA32LibmTanfSvcImmediate;
using liba32android::compat::kA32LibmTanhSvcImmediate;
using liba32android::compat::kA32LibmTruncSvcImmediate;
using liba32android::compat::kA32LibmTruncfSvcImmediate;
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
    result.max_relocations = 128U;
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

bool exact_float_call(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::string_view wrapper,
    float input,
    float expected) {
    const auto result = run_wrapper(
        memory, graph, wrapper, registry, stack_top, stop_pc,
        std::bit_cast<std::uint32_t>(input));
    return result.has_value() && *result &&
           result->services_handled == 1U &&
           std::bit_cast<float>(result->regs[0]) == expected;
}

bool exact_binary_double_call(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::string_view wrapper,
    double first,
    double second,
    double expected) {
    const auto first_words = double_words(first);
    const auto second_words = double_words(second);
    const auto result = run_wrapper(
        memory, graph, wrapper, registry, stack_top, stop_pc,
        first_words[0], first_words[1], second_words[0], second_words[1]);
    return result.has_value() && *result &&
           result->services_handled == 1U &&
           result_double(*result) == expected;
}

bool exact_binary_float_call(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::string_view wrapper,
    float first,
    float second,
    float expected) {
    const auto result = run_wrapper(
        memory, graph, wrapper, registry, stack_top, stop_pc,
        std::bit_cast<std::uint32_t>(first),
        std::bit_cast<std::uint32_t>(second));
    return result.has_value() && *result &&
           result->services_handled == 1U &&
           std::bit_cast<float>(result->regs[0]) == expected;
}

std::int64_t result_i64(const A32ServiceDispatchResult& result) {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(result.regs[0]) |
        (static_cast<std::uint64_t>(result.regs[1]) << 32U);
    return std::bit_cast<std::int64_t>(bits);
}

std::optional<float> read_f32(
    const MappedGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return std::nullopt;
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return std::bit_cast<float>(bits);
}

std::optional<double> read_f64(
    const MappedGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 8> bytes{};
    if (!memory.read(address, bytes)) return std::nullopt;
    std::uint64_t bits{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bits |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return std::bit_cast<double>(bits);
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

    constexpr std::array<std::string_view, 66> names{{
        "acos", "asin", "atan2", "cos", "cosf", "exp",
        "floor", "frexp", "ldexp", "log", "log10", "log10f",
        "pow", "powf", "sin", "sinf", "tan", "acosf",
        "atan", "atan2f", "atanf", "cbrt", "cbrtf", "ceil",
        "ceilf", "cosh", "exp2", "exp2f", "expf", "expm1",
        "fabs", "floorf", "fmax", "fmaxf", "fminf", "fmod",
        "fmodf", "frexpf", "hypot", "hypotf", "ldexpf", "llrint",
        "llrintf", "llround", "llroundf", "log1p", "logf", "lrint",
        "lrintf", "lround", "lroundf", "modf", "modff", "nanf",
        "rint", "rintf", "round", "roundf", "scalbn", "sincos",
        "sincosf", "sinh", "tanf", "tanh", "trunc", "truncf",
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
        return fail("libm consumer did not receive all eager relocations");
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
    const std::array<A32HostServiceRegistryEntry, 66> services{{
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
        {kA32LibmAcosfSvcImmediate, &service},
        {kA32LibmAtanSvcImmediate, &service},
        {kA32LibmAtan2fSvcImmediate, &service},
        {kA32LibmAtanfSvcImmediate, &service},
        {kA32LibmCbrtSvcImmediate, &service},
        {kA32LibmCbrtfSvcImmediate, &service},
        {kA32LibmCeilSvcImmediate, &service},
        {kA32LibmCeilfSvcImmediate, &service},
        {kA32LibmCoshSvcImmediate, &service},
        {kA32LibmExp2SvcImmediate, &service},
        {kA32LibmExp2fSvcImmediate, &service},
        {kA32LibmExpfSvcImmediate, &service},
        {kA32LibmExpm1SvcImmediate, &service},
        {kA32LibmFabsSvcImmediate, &service},
        {kA32LibmFloorfSvcImmediate, &service},
        {kA32LibmFmaxSvcImmediate, &service},
        {kA32LibmFmaxfSvcImmediate, &service},
        {kA32LibmFminfSvcImmediate, &service},
        {kA32LibmFmodSvcImmediate, &service},
        {kA32LibmFmodfSvcImmediate, &service},
        {kA32LibmFrexpfSvcImmediate, &service},
        {kA32LibmHypotSvcImmediate, &service},
        {kA32LibmHypotfSvcImmediate, &service},
        {kA32LibmLdexpfSvcImmediate, &service},
        {kA32LibmLlrintSvcImmediate, &service},
        {kA32LibmLlrintfSvcImmediate, &service},
        {kA32LibmLlroundSvcImmediate, &service},
        {kA32LibmLlroundfSvcImmediate, &service},
        {kA32LibmLog1pSvcImmediate, &service},
        {kA32LibmLogfSvcImmediate, &service},
        {kA32LibmLrintSvcImmediate, &service},
        {kA32LibmLrintfSvcImmediate, &service},
        {kA32LibmLroundSvcImmediate, &service},
        {kA32LibmLroundfSvcImmediate, &service},
        {kA32LibmModfSvcImmediate, &service},
        {kA32LibmModffSvcImmediate, &service},
        {kA32LibmNanfSvcImmediate, &service},
        {kA32LibmRintSvcImmediate, &service},
        {kA32LibmRintfSvcImmediate, &service},
        {kA32LibmRoundSvcImmediate, &service},
        {kA32LibmRoundfSvcImmediate, &service},
        {kA32LibmScalbnSvcImmediate, &service},
        {kA32LibmSincosSvcImmediate, &service},
        {kA32LibmSincosfSvcImmediate, &service},
        {kA32LibmSinhSvcImmediate, &service},
        {kA32LibmTanfSvcImmediate, &service},
        {kA32LibmTanhSvcImmediate, &service},
        {kA32LibmTruncSvcImmediate, &service},
        {kA32LibmTruncfSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{services}};
    std::size_t completed_calls = 0U;

    const struct {
        const char* wrapper;
        double input;
        double expected;
    } unary_double_cases[] = {
        {"fixture_acos", 1, 0},
        {"fixture_asin", 0, 0},
        {"fixture_atan", 0, 0},
        {"fixture_cbrt", 8, 2},
        {"fixture_ceil", 1.25, 2},
        {"fixture_cos", 0, 1},
        {"fixture_cosh", 0, 1},
        {"fixture_exp", 0, 1},
        {"fixture_exp2", 3, 8},
        {"fixture_expm1", 0, 0},
        {"fixture_fabs", -2, 2},
        {"fixture_floor", 1.75, 1},
        {"fixture_log", 1, 0},
        {"fixture_log10", 1, 0},
        {"fixture_log1p", 0, 0},
        {"fixture_rint", 2, 2},
        {"fixture_round", 1.5, 2},
        {"fixture_sin", 0, 0},
        {"fixture_sinh", 0, 0},
        {"fixture_tan", 0, 0},
        {"fixture_tanh", 0, 0},
        {"fixture_trunc", 1.75, 1},
    };
    for (const auto& test_case : unary_double_cases) {
        if (!exact_double_call(
                memory, loaded.graph, registry, stack_top, *stop,
                test_case.wrapper, test_case.input, test_case.expected)) {
            return fail(std::string("real libm unary-double wrapper failed: ") +
                        test_case.wrapper);
        }
        ++completed_calls;
    }

    const struct {
        const char* wrapper;
        float input;
        float expected;
    } unary_float_cases[] = {
        {"fixture_acosf", 1.0f, 0.0f},
        {"fixture_atanf", 0.0f, 0.0f},
        {"fixture_cbrtf", 8.0f, 2.0f},
        {"fixture_ceilf", 1.25f, 2.0f},
        {"fixture_cosf", 0.0f, 1.0f},
        {"fixture_exp2f", 3.0f, 8.0f},
        {"fixture_expf", 0.0f, 1.0f},
        {"fixture_floorf", 1.75f, 1.0f},
        {"fixture_log10f", 1.0f, 0.0f},
        {"fixture_logf", 1.0f, 0.0f},
        {"fixture_rintf", 2.0f, 2.0f},
        {"fixture_roundf", 1.5f, 2.0f},
        {"fixture_sinf", 0.0f, 0.0f},
        {"fixture_tanf", 0.0f, 0.0f},
        {"fixture_truncf", 1.75f, 1.0f},
    };
    for (const auto& test_case : unary_float_cases) {
        if (!exact_float_call(
                memory, loaded.graph, registry, stack_top, *stop,
                test_case.wrapper, test_case.input, test_case.expected)) {
            return fail(std::string("real libm unary-float wrapper failed: ") +
                        test_case.wrapper);
        }
        ++completed_calls;
    }

    const struct {
        const char* wrapper;
        double first;
        double second;
        double expected;
    } binary_double_cases[] = {
        {"fixture_atan2", 0, 1, 0},
        {"fixture_fmax", 2, 3, 3},
        {"fixture_fmod", 5, 2, 1},
        {"fixture_hypot", 3, 4, 5},
        {"fixture_pow", 2, 3, 8},
    };
    for (const auto& test_case : binary_double_cases) {
        if (!exact_binary_double_call(
                memory, loaded.graph, registry, stack_top, *stop,
                test_case.wrapper, test_case.first, test_case.second,
                test_case.expected)) {
            return fail(std::string("real libm binary-double wrapper failed: ") +
                        test_case.wrapper);
        }
        ++completed_calls;
    }

    const struct {
        const char* wrapper;
        float first;
        float second;
        float expected;
    } binary_float_cases[] = {
        {"fixture_atan2f", 0.0f, 1.0f, 0.0f},
        {"fixture_fmaxf", 2.0f, 3.0f, 3.0f},
        {"fixture_fminf", 2.0f, 3.0f, 2.0f},
        {"fixture_fmodf", 5.0f, 2.0f, 1.0f},
        {"fixture_hypotf", 3.0f, 4.0f, 5.0f},
        {"fixture_powf", 2.0f, 3.0f, 8.0f},
    };
    for (const auto& test_case : binary_float_cases) {
        if (!exact_binary_float_call(
                memory, loaded.graph, registry, stack_top, *stop,
                test_case.wrapper, test_case.first, test_case.second,
                test_case.expected)) {
            return fail(std::string("real libm binary-float wrapper failed: ") +
                        test_case.wrapper);
        }
        ++completed_calls;
    }

    const std::uint32_t frexp_exponent = *data + 0x100U;
    auto first = double_words(8.0);
    auto result = run_wrapper(
        memory, loaded.graph, "fixture_frexp", registry, stack_top, *stop,
        first[0], first[1], frexp_exponent);
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 0.5 ||
        read_i32(memory, frexp_exponent) != 4) {
        return fail("real libm frexp wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t frexpf_exponent = *data + 0x104U;
    result = run_wrapper(
        memory, loaded.graph, "fixture_frexpf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(8.0f), frexpf_exponent);
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 0.5f ||
        read_i32(memory, frexpf_exponent) != 4) {
        return fail("real libm frexpf wrapper failed");
    }
    ++completed_calls;

    first = double_words(0.5);
    for (const auto wrapper : {"fixture_ldexp", "fixture_scalbn"}) {
        result = run_wrapper(
            memory, loaded.graph, wrapper, registry, stack_top, *stop,
            first[0], first[1],
            std::bit_cast<std::uint32_t>(std::int32_t{4}));
        if (!result || !*result || result->services_handled != 1U ||
            result_double(*result) != 8.0) {
            return fail(std::string("real libm double+int wrapper failed: ") +
                        wrapper);
        }
        ++completed_calls;
    }

    result = run_wrapper(
        memory, loaded.graph, "fixture_ldexpf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(0.5f),
        std::bit_cast<std::uint32_t>(std::int32_t{4}));
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 8.0f) {
        return fail("real libm ldexpf wrapper failed");
    }
    ++completed_calls;

    first = double_words(2.0);
    for (const auto wrapper : {"fixture_llrint", "fixture_llround"}) {
        result = run_wrapper(
            memory, loaded.graph, wrapper, registry, stack_top, *stop,
            first[0], first[1]);
        if (!result || !*result || result->services_handled != 1U ||
            result_i64(*result) != 2) {
            return fail(std::string("real libm double->int64 wrapper failed: ") +
                        wrapper);
        }
        ++completed_calls;
    }

    for (const auto wrapper : {"fixture_llrintf", "fixture_llroundf"}) {
        result = run_wrapper(
            memory, loaded.graph, wrapper, registry, stack_top, *stop,
            std::bit_cast<std::uint32_t>(2.0f));
        if (!result || !*result || result->services_handled != 1U ||
            result_i64(*result) != 2) {
            return fail(std::string("real libm float->int64 wrapper failed: ") +
                        wrapper);
        }
        ++completed_calls;
    }

    first = double_words(2.0);
    for (const auto wrapper : {"fixture_lrint", "fixture_lround"}) {
        result = run_wrapper(
            memory, loaded.graph, wrapper, registry, stack_top, *stop,
            first[0], first[1]);
        if (!result || !*result || result->services_handled != 1U ||
            std::bit_cast<std::int32_t>(result->regs[0]) != 2) {
            return fail(std::string("real libm double->long wrapper failed: ") +
                        wrapper);
        }
        ++completed_calls;
    }

    for (const auto wrapper : {"fixture_lrintf", "fixture_lroundf"}) {
        result = run_wrapper(
            memory, loaded.graph, wrapper, registry, stack_top, *stop,
            std::bit_cast<std::uint32_t>(2.0f));
        if (!result || !*result || result->services_handled != 1U ||
            std::bit_cast<std::int32_t>(result->regs[0]) != 2) {
            return fail(std::string("real libm float->long wrapper failed: ") +
                        wrapper);
        }
        ++completed_calls;
    }

    const std::uint32_t modf_integral = *data + 0x108U;
    first = double_words(1.5);
    result = run_wrapper(
        memory, loaded.graph, "fixture_modf", registry, stack_top, *stop,
        first[0], first[1], modf_integral);
    const auto modf_value = read_f64(memory, modf_integral);
    if (!result || !*result || result->services_handled != 1U ||
        result_double(*result) != 0.5 || !modf_value || *modf_value != 1.0) {
        return fail("real libm modf wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t modff_integral = *data + 0x110U;
    result = run_wrapper(
        memory, loaded.graph, "fixture_modff", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(1.5f), modff_integral);
    const auto modff_value = read_f32(memory, modff_integral);
    if (!result || !*result || result->services_handled != 1U ||
        std::bit_cast<float>(result->regs[0]) != 0.5f ||
        !modff_value || *modff_value != 1.0f) {
        return fail("real libm modff wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t nan_tag = *data + 0x118U;
    constexpr std::array<std::uint8_t, 1> empty_nan_tag{{0U}};
    if (!memory.write(nan_tag, empty_nan_tag)) {
        return fail("could not stage real libm nanf tag");
    }
    result = run_wrapper(
        memory, loaded.graph, "fixture_nanf", registry, stack_top, *stop,
        nan_tag);
    if (!result || !*result || result->services_handled != 1U ||
        !std::isnan(std::bit_cast<float>(result->regs[0]))) {
        return fail("real libm nanf wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t sincos_sine = *data + 0x120U;
    const std::uint32_t sincos_cosine = *data + 0x128U;
    first = double_words(0.0);
    result = run_wrapper(
        memory, loaded.graph, "fixture_sincos", registry, stack_top, *stop,
        first[0], first[1], sincos_sine, sincos_cosine);
    const auto sincos_sine_value = read_f64(memory, sincos_sine);
    const auto sincos_cosine_value = read_f64(memory, sincos_cosine);
    if (!result || !*result || result->services_handled != 1U ||
        !sincos_sine_value || *sincos_sine_value != 0.0 ||
        !sincos_cosine_value || *sincos_cosine_value != 1.0) {
        return fail("real libm sincos wrapper failed");
    }
    ++completed_calls;

    const std::uint32_t sincosf_sine = *data + 0x130U;
    const std::uint32_t sincosf_cosine = *data + 0x134U;
    result = run_wrapper(
        memory, loaded.graph, "fixture_sincosf", registry, stack_top, *stop,
        std::bit_cast<std::uint32_t>(0.0f), sincosf_sine, sincosf_cosine);
    const auto sincosf_sine_value = read_f32(memory, sincosf_sine);
    const auto sincosf_cosine_value = read_f32(memory, sincosf_cosine);
    if (!result || !*result || result->services_handled != 1U ||
        !sincosf_sine_value || *sincosf_sine_value != 0.0f ||
        !sincosf_cosine_value || *sincosf_cosine_value != 1.0f) {
        return fail("real libm sincosf wrapper failed");
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
