#include <array>
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

#include "compat/a32_android_log_shim.h"
#include "compat/a32_android_log_write.h"
#include "compat/a32_android_namespace_policy.h"
#include "compat/a32_android_platform_provider.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::compat::A32AndroidLogFormatOptions;
using liba32android::compat::A32AndroidLogPrintService;
using liba32android::compat::A32AndroidLogSink;
using liba32android::compat::A32AndroidLogWriteOptions;
using liba32android::compat::A32AndroidLogWriteService;
using liba32android::compat::A32AndroidNamespaceAccessPolicy;
using liba32android::compat::A32AndroidNamespaceBinding;
using liba32android::compat::A32AndroidNamespaceLink;
using liba32android::compat::A32AndroidPlatformProvider;
using liba32android::compat::kA32AndroidLogPrintShimSvcImmediate;
using liba32android::compat::kA32AndroidLogShimIdentity;
using liba32android::compat::kA32AndroidLogShimSoname;
using liba32android::compat::kA32AndroidLogVprintShimSvcImmediate;
using liba32android::compat::kA32AndroidLogWriteShimSvcImmediate;
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
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
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

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 256,
        .max_hash_buckets = 256,
        .max_gnu_bloom_words = 64,
        .max_scope_objects = 8,
        .max_name_bytes = kMaxFixtureNameBytes,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("Android log shim symbol lookup failed for ") +
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

class RecordingSink final : public A32AndroidLogSink {
public:
    std::size_t calls{};
    std::int32_t priority{};
    std::optional<std::string> tag;
    std::string text;

    std::int32_t write(
        std::int32_t value,
        std::optional<std::string_view> tag_value,
        std::string_view text_value) override {
        ++calls;
        priority = value;
        tag = tag_value.has_value()
                  ? std::optional<std::string>{std::string{*tag_value}}
                  : std::nullopt;
        text.assign(text_value.data(), text_value.size());
        return 1;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail("expected paths to ARM32 Android log consumer and liblog.so shim");
    }

    const std::vector<std::uint8_t> consumer_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> shim_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (consumer_image.empty() || shim_image.empty()) {
        return fail("generated ARM32 Android log shim fixture is missing or empty");
    }

    MappedGuestMemory memory;

    const std::array<Elf32DependencyCatalogEntry, 0> app_entries{};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};

    const std::array<A32AndroidNamespaceBinding, 1> namespace_bindings{{
        {"android-log-consumer", "app"},
    }};
    const std::array<std::string_view, 1> platform_shared_libs{{
        kA32AndroidLogShimSoname,
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
    A32AndroidPlatformProvider platform_provider{
        std::span{shim_image}, namespace_policy};
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
            .identity = "android-log-consumer",
            .image = consumer_image,
        },
        provider_chain,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("Android log shim dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 2) {
        return fail("Android namespace-gated platform fallback did not form the expected graph");
    }

    const auto& consumer = graph_result.graph.objects[0];
    const auto& shim = graph_result.graph.objects[1];
    if (consumer.linker_strings.needed.size() != 1 ||
        consumer.linker_strings.needed[0] != kA32AndroidLogShimSoname ||
        consumer.dependencies.size() != 1 ||
        consumer.dependencies[0].requested_name != kA32AndroidLogShimSoname ||
        consumer.dependencies[0].target_object != 1 ||
        shim.identity != kA32AndroidLogShimIdentity ||
        !shim.linker_strings.needed.empty()) {
        return fail("Android log shim dependency/provider metadata was incorrect");
    }

    const auto lookup_options = symbol_options();
    const auto call_write = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_android_log_write",
        lookup_options);
    const auto call_print = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_android_log_print",
        lookup_options);
    const auto call_vprint = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_android_log_vprint",
        lookup_options);
    const auto log_write = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "__android_log_write",
        lookup_options);
    const auto log_print = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "__android_log_print",
        lookup_options);
    const auto log_vprint = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "__android_log_vprint",
        lookup_options);
    if (!call_write) {
        return fail(lookup_error("fixture_android_log_write", call_write));
    }
    if (!call_print) {
        return fail(lookup_error("fixture_android_log_print", call_print));
    }
    if (!call_vprint) {
        return fail(lookup_error("fixture_android_log_vprint", call_vprint));
    }
    if (!log_write) return fail(lookup_error("__android_log_write", log_write));
    if (!log_print) return fail(lookup_error("__android_log_print", log_print));
    if (!log_vprint) {
        return fail(lookup_error("__android_log_vprint", log_vprint));
    }
    if (call_write.symbol.object_index != 0 ||
        call_print.symbol.object_index != 0 ||
        call_vprint.symbol.object_index != 0 ||
        log_write.symbol.object_index != 1 ||
        log_print.symbol.object_index != 1 ||
        log_vprint.symbol.object_index != 1) {
        return fail("Android log shim symbols resolved from the wrong graph objects");
    }

    const auto relocated = apply_elf32_combined_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!relocated || relocated.application.writes.size() != 3U) {
        return fail(
            std::string("Android log shim consumer relocation failed: ") +
            liba32android::elf::to_string(relocated.error));
    }
    std::array<bool, 3> targets_seen{};
    const std::array<std::uint32_t, 3> expected_targets{{
        log_write.symbol.symbol.guest_value,
        log_print.symbol.symbol.guest_value,
        log_vprint.symbol.symbol.guest_value,
    }};
    for (const auto& write : relocated.application.writes) {
        if (write.type != kRArmJumpSlot) {
            return fail("Android log consumer emitted a non-JUMP_SLOT import");
        }
        bool matched = false;
        for (std::size_t i = 0; i < expected_targets.size(); ++i) {
            if (write.final_word == expected_targets[i] && !targets_seen[i]) {
                targets_seen[i] = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return fail("Android log consumer relocation targeted unexpected symbol");
        }
    }
    if (!targets_seen[0] || !targets_seen[1] || !targets_seen[2]) {
        return fail("Android log consumer did not relocate all log entrypoints");
    }

    const auto data = find_unmapped_region(memory, 0x70000000U, 1);
    const auto stack = find_unmapped_region(memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(memory, 0x72000000U, 1);
    if (!data.has_value() || !stack.has_value() || !stop.has_value()) {
        return fail("could not reserve Android log shim execution harness regions");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*data, memory.page_size(), rw) ||
        !memory.map(*stack, memory.page_size() * kStackPages, rw)) {
        return fail("could not map Android log shim data/stack");
    }

    constexpr std::array<std::uint8_t, 7> tag{
        'L', 'i', 'b', 'A', '3', '2', 0,
    };
    constexpr std::array<std::uint8_t, 22> text_bytes{
        'h','e','l','l','o',' ','f','r','o','m',' ',
        'g','u','e','s','t',' ','s','h','i','m',0,
    };
    const std::uint32_t tag_address = *data;
    const std::uint32_t text_address = *data + 0x100U;
    if (!memory.write(tag_address, tag) ||
        !memory.write(text_address, text_bytes)) {
        return fail("could not stage Android log shim guest strings");
    }

    for (const auto* call : {&call_write, &call_print, &call_vprint}) {
        if (call->symbol.symbol.symbol.type != 2U ||
            call->symbol.symbol.symbol.size == 0U) {
            return fail("Android log fixture wrapper was not a non-empty STT_FUNC");
        }
    }

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("Android log shim stack top overflowed guest address space");
    }

    RecordingSink sink;
    A32AndroidLogWriteService write_service{
        kA32AndroidLogWriteShimSvcImmediate,
        sink,
        A32AndroidLogWriteOptions{32, 128},
    };
    A32AndroidLogPrintService print_service{
        kA32AndroidLogPrintShimSvcImmediate,
        kA32AndroidLogVprintShimSvcImmediate,
        sink,
        A32AndroidLogFormatOptions{
            .max_tag_bytes = 32U,
            .max_format_bytes = 128U,
            .max_output_bytes = 256U,
            .max_string_argument_bytes = 128U,
            .max_arguments = 16U,
            .max_field_width = 64U,
            .max_precision = 64U,
        },
    };
    const std::array<A32HostServiceRegistryEntry, 3> services{{
        {kA32AndroidLogWriteShimSvcImmediate, &write_service},
        {kA32AndroidLogPrintShimSvcImmediate, &print_service},
        {kA32AndroidLogVprintShimSvcImmediate, &print_service},
    }};
    A32HostServiceRegistry registry{std::span{services}};

    const auto execute_wrapper =
        [&](const Elf32GraphSymbolLookupResult& call)
            -> std::optional<liba32android::runtime::A32ServiceDispatchResult> {
            const auto& symbol = call.symbol.symbol;
            const bool thumb = (symbol.symbol.value & 1U) != 0U;
            ExecutionRequest request{};
            request.instruction_set =
                thumb ? InstructionSet::Thumb : InstructionSet::Arm;
            request.entry_pc = symbol.guest_value & ~1U;
            request.regs[0] = tag_address;
            request.regs[1] = text_address;
            request.regs[13] =
                static_cast<std::uint32_t>(stack_top64) & ~7U;
            request.regs[14] = *stop | (thumb ? 1U : 0U);
            request.instruction_count = kInstructionBudget;
            request.stop_pc = *stop;
            return execute_a32_with_services(memory, request, registry, 1);
        };

    std::size_t completed_service_calls = 0U;
    auto result = execute_wrapper(call_write);
    if (!result || !result->stop_pc_reached ||
        result->services_handled != 1U ||
        result->regs[0] != 1U ||
        sink.calls != 1U ||
        sink.priority != 4 ||
        sink.tag != std::optional<std::string>{"LibA32"} ||
        sink.text != "hello from guest shim") {
        return fail("__android_log_write integration failed");
    }
    completed_service_calls += result->services_handled;

    result = execute_wrapper(call_print);
    if (!result || !result->stop_pc_reached ||
        result->services_handled != 1U ||
        result->regs[0] != 1U ||
        sink.calls != 2U ||
        sink.priority != 5 ||
        sink.tag != std::optional<std::string>{"LibA32"} ||
        sink.text != "guest=hello from guest shim value=-7 hex=0x2a") {
        return fail("__android_log_print integration failed");
    }
    completed_service_calls += result->services_handled;

    result = execute_wrapper(call_vprint);
    if (!result || !result->stop_pc_reached ||
        result->services_handled != 1U ||
        result->regs[0] != 1U ||
        sink.calls != 3U ||
        sink.priority != 6 ||
        sink.tag != std::optional<std::string>{"LibA32"} ||
        sink.text != "width=     hel signed=-2 ptr=0x1234") {
        return fail("__android_log_vprint integration failed");
    }
    completed_service_calls += result->services_handled;

    std::cout
        << "fixture.android_log.object_count="
        << graph_result.graph.objects.size() << '\n'
        << "fixture.android_log.needed=" << kA32AndroidLogShimSoname << '\n'
        << "fixture.android_log.namespace_access=linked\n"
        << "fixture.android_log.symbol_object="
        << log_write.symbol.object_index << '\n'
        << "fixture.android_log.relocation_count="
        << relocated.application.writes.size() << '\n'
        << "fixture.android_log.write_service_id=0x" << std::hex
        << kA32AndroidLogWriteShimSvcImmediate
        << "\nfixture.android_log.print_service_id=0x"
        << kA32AndroidLogPrintShimSvcImmediate
        << "\nfixture.android_log.vprint_service_id=0x"
        << kA32AndroidLogVprintShimSvcImmediate << std::dec << '\n'
        << "fixture.android_log.service_calls="
        << completed_service_calls << '\n'
        << "fixture.android_log.priority=" << sink.priority << '\n'
        << "fixture.android_log.status=PASS\n";
    return 0;
}
