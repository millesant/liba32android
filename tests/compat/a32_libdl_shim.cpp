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

#include "compat/a32_android_namespace_policy.h"
#include "compat/a32_android_platform_provider.h"
#include "compat/a32_aeabi_atexit.h"
#include "compat/a32_libdl.h"
#include "compat/a32_libdl_close_transaction.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_link_map.h"
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
using liba32android::compat::A32AeabiAtexitService;
using liba32android::compat::A32LibDlCloseTransaction;
using liba32android::compat::A32LibDlCloseTransactionOptions;
using liba32android::compat::A32LibDlHandle;
using liba32android::compat::A32LibDlObjectLifecycleBinding;
using liba32android::compat::A32LibDlOptions;
using liba32android::compat::A32LibDlService;
using liba32android::compat::kA32LibDlDladdrSvcImmediate;
using liba32android::compat::kA32LibDlDlcloseSvcImmediate;
using liba32android::compat::kA32LibDlDlerrorSvcImmediate;
using liba32android::compat::kA32LibDlDlopenSvcImmediate;
using liba32android::compat::kA32LibDlDlsymSvcImmediate;
using liba32android::compat::kA32LibDlShimIdentity;
using liba32android::compat::kA32LibDlShimSoname;
using liba32android::compat::kA32RtldNow;
using liba32android::compat::make_a32_libdl_shim_catalog_entry;
using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::elf::Elf32DependencyCatalogEntry;
using liba32android::elf::Elf32DependencyCatalogProvider;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderChain;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32LinkMapRootPolicy;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::append_elf32_link_map_root;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;
using liba32android::runtime::A32ServiceDispatchResult;
using liba32android::runtime::execute_a32_with_services;

constexpr std::uint32_t kMaxFixtureNameBytes = 128U;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 12U << 20;
constexpr std::size_t kInstructionBudget = 256U;
constexpr std::size_t kStackPages = 4U;
constexpr std::string_view kProviderSoname = "libfixture_dl_target.so";
constexpr std::string_view kProviderIdentity = "libdl-fixture-provider";

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 512U,
        .max_hash_buckets = 512U,
        .max_gnu_bloom_words = 128U,
        .max_scope_objects = 16U,
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
    return std::string("libdl shim symbol lookup failed for ") +
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

std::string read_c_string(
    const MappedGuestMemory& memory,
    std::uint32_t address,
    std::size_t max_bytes) {
    std::string result;
    for (std::size_t i = 0; i <= max_bytes; ++i) {
        if (i > std::numeric_limits<std::uint32_t>::max() - address) {
            return {};
        }
        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(
                address + static_cast<std::uint32_t>(i), byte)) {
            return {};
        }
        if (byte[0] == 0U) {
            return result;
        }
        result.push_back(static_cast<char>(byte[0]));
    }
    return {};
}

std::optional<A32ServiceDispatchResult> run_wrapper(
    MappedGuestMemory& memory,
    const liba32android::elf::Elf32DependencyGraph& graph,
    std::string_view name,
    A32HostServiceRegistry& registry,
    std::uint32_t stack_top,
    std::uint32_t stop_pc,
    std::uint32_t r0 = 0U,
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        return fail(
            "expected ARM32 libdl consumer, libdl.so shim, and resident provider paths");
    }

    const std::vector<std::uint8_t> consumer_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> shim_image =
        liba32android::test_support::read_binary_file(argv[2]);
    const std::vector<std::uint8_t> provider_image =
        liba32android::test_support::read_binary_file(argv[3]);
    if (consumer_image.empty() || shim_image.empty() ||
        provider_image.empty()) {
        return fail("generated ARM32 libdl fixture is missing or empty");
    }

    MappedGuestMemory memory;

    const std::array<Elf32DependencyCatalogEntry, 1> app_entries{{
        {
            .requested_name = kProviderSoname,
            .identity = kProviderIdentity,
            .image = std::span<const std::uint8_t>{provider_image},
        },
    }};
    Elf32DependencyCatalogProvider app_provider{std::span{app_entries}};

    const std::array<A32AndroidNamespaceBinding, 1> namespace_bindings{{
        {"libdl-consumer", "app"},
    }};
    const std::array<std::string_view, 1> platform_shared_libs{{
        kA32LibDlShimSoname,
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
    const std::array<Elf32DependencyCatalogEntry, 1> platform_entries{{
        make_a32_libdl_shim_catalog_entry(
            std::span<const std::uint8_t>{shim_image}),
    }};
    A32AndroidPlatformCatalogProvider platform_provider{
        std::span{platform_entries},
        namespace_policy,
    };
    const std::array<Elf32DependencyProvider*, 2> providers{{
        &app_provider,
        &platform_provider,
    }};
    Elf32DependencyProviderChain provider_chain{std::span{providers}};

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 8U;
    load_options.max_depth = 8U;
    load_options.max_dependency_occurrences = 16U;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxTotalImageBytes;
    load_options.max_string_bytes = kMaxFixtureNameBytes;

    Elf32LinkMap link_map;
    const auto loaded = append_elf32_link_map_root(
        memory,
        link_map,
        Elf32DependencyLoadSource{
            .identity = "libdl-consumer",
            .image = consumer_image,
        },
        provider_chain,
        load_options,
        Elf32LinkMapRootPolicy::Local);
    if (!loaded || !loaded.root_object_index.has_value() ||
        *loaded.root_object_index != 0U ||
        link_map.graph.objects.size() != 3U) {
        return fail("libdl persistent link-map fixture did not load three objects");
    }

    std::optional<std::size_t> shim_index;
    std::optional<std::size_t> provider_index;
    for (std::size_t index = 0; index < link_map.graph.objects.size(); ++index) {
        const auto& object = link_map.graph.objects[index];
        if (object.identity == kA32LibDlShimIdentity) {
            shim_index = index;
        }
        if (object.identity == kProviderIdentity) {
            provider_index = index;
        }
    }
    if (!shim_index.has_value() || !provider_index.has_value()) {
        return fail("libdl fixture object identities were not retained");
    }

    constexpr std::array<std::string_view, 5> libdl_names{{
        "dlopen", "dlsym", "dlclose", "dlerror", "dladdr",
    }};
    for (const auto name : libdl_names) {
        const auto symbol = lookup_elf32_graph_symbol(
            memory, link_map.graph, 0U, name, symbol_options());
        if (!symbol) {
            return fail(lookup_error(name, symbol));
        }
        if (symbol.symbol.object_index != *shim_index) {
            return fail("libdl symbol resolved from wrong graph object");
        }
    }

    const auto provider_symbol = lookup_elf32_graph_symbol(
        memory,
        link_map.graph,
        0U,
        "fixture_dynamic_value",
        symbol_options());
    if (!provider_symbol ||
        provider_symbol.symbol.object_index != *provider_index) {
        return fail("resident provider symbol did not resolve from provider object");
    }

    const auto close_marker_symbol = lookup_elf32_graph_symbol(
        memory,
        link_map.graph,
        0U,
        "fixture_dlclose_marker",
        symbol_options());
    const auto dso_handle_symbol = lookup_elf32_graph_symbol(
        memory,
        link_map.graph,
        0U,
        "fixture_dlclose_dso_handle",
        symbol_options());
    if (!close_marker_symbol || !dso_handle_symbol ||
        close_marker_symbol.symbol.object_index != *provider_index ||
        dso_handle_symbol.symbol.object_index != *provider_index ||
        dso_handle_symbol.symbol.symbol.guest_value == 0U) {
        return fail("resident provider teardown symbols were not retained");
    }

    for (std::size_t object_index = 0U;
         object_index < link_map.graph.objects.size();
         ++object_index) {
        const auto relocated = apply_elf32_combined_relocations(
            memory, link_map.graph, object_index, relocation_options());
        if (!relocated) {
            return fail(
                std::string("libdl graph relocation failed for object ") +
                std::to_string(object_index) + ": " +
                liba32android::elf::to_string(relocated.error));
        }
    }

    const auto data = find_unmapped_region(memory, 0x70000000U, 1U);
    const auto stack = find_unmapped_region(
        memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(memory, 0x72000000U, 1U);
    if (!data.has_value() || !stack.has_value() || !stop.has_value()) {
        return fail("could not reserve libdl integration harness regions");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*data, memory.page_size(), rw) ||
        !memory.map(*stack, memory.page_size() * kStackPages, rw)) {
        return fail("could not map libdl integration data/stack");
    }

    const std::uint32_t library_name = *data;
    const std::uint32_t symbol_name = *data + 0x100U;
    const std::uint32_t dl_info = *data + 0x200U;
    const std::string provider_name{kProviderSoname};
    const std::string target_symbol{"fixture_dynamic_value"};
    std::vector<std::uint8_t> provider_name_bytes(
        provider_name.begin(), provider_name.end());
    provider_name_bytes.push_back(0U);
    std::vector<std::uint8_t> target_symbol_bytes(
        target_symbol.begin(), target_symbol.end());
    target_symbol_bytes.push_back(0U);
    if (!memory.write(library_name, provider_name_bytes) ||
        !memory.write(symbol_name, target_symbol_bytes)) {
        return fail("could not stage libdl guest lookup strings");
    }

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("libdl integration stack top overflowed guest range");
    }
    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(stack_top64) & ~7U;

    std::array<A32LibDlHandle, 8> handles{};
    std::array<A32AeabiAtexitRecord, 1> atexit_records{};
    A32AeabiAtexitService registrations{std::span{atexit_records}};

    Elf32LifecycleState lifecycle;
    lifecycle.objects.resize(link_map.graph.objects.size());
    for (auto& object_state : lifecycle.objects) {
        object_state.constructors = Elf32LifecycleObjectStatus::Complete;
    }

    const std::array<A32LibDlObjectLifecycleBinding, 1> close_bindings{{
        {
            .object_index = *provider_index,
            .dso_handle = dso_handle_symbol.symbol.symbol.guest_value,
        },
    }};
    const std::array<A32HostServiceRegistryEntry, 0> lifecycle_services{};
    A32HostServiceRegistry lifecycle_registry{std::span{lifecycle_services}};
    A32LibDlCloseTransaction close_transaction{
        link_map,
        std::span{handles},
        lifecycle,
        registrations,
        std::span{close_bindings},
        A32LibDlCloseTransactionOptions{
            .max_fini_array_entries = 4U,
            .execution = {
                .stack_top = stack_top,
                .return_pc = *stop,
                .max_instructions_per_call = kInstructionBudget,
                .service_handler = &lifecycle_registry,
                .max_service_calls_per_call = 1U,
            },
        },
    };

    A32LibDlOptions dl_options;
    dl_options.max_name_bytes = kMaxFixtureNameBytes;
    dl_options.handle_base = 0x7f000000U;
    dl_options.error_buffer_address = *data + 0x800U;
    dl_options.error_buffer_bytes = 128U;
    dl_options.info_string_buffer_address = *data + 0x900U;
    dl_options.info_string_buffer_bytes = 512U;
    dl_options.symbols = symbol_options();
    A32LibDlService service{
        link_map,
        std::span{handles},
        dl_options,
        &close_transaction,
    };
    const std::array<A32HostServiceRegistryEntry, 5> services{{
        {kA32LibDlDlopenSvcImmediate, &service},
        {kA32LibDlDlsymSvcImmediate, &service},
        {kA32LibDlDlcloseSvcImmediate, &service},
        {kA32LibDlDlerrorSvcImmediate, &service},
        {kA32LibDlDladdrSvcImmediate, &service},
    }};
    A32HostServiceRegistry registry{std::span{services}};

    std::size_t service_calls = 0U;

    auto result = run_wrapper(
        memory, link_map.graph, "fixture_direct_provider_call", registry,
        stack_top, *stop);
    if (!result || !*result || result->regs[0] != 77U ||
        result->services_handled != 0U) {
        return fail("direct resident provider call failed after relocation");
    }

    result = run_wrapper(
        memory, link_map.graph, "fixture_dlopen", registry,
        stack_top, *stop, library_name, kA32RtldNow);
    if (!result || !*result || result->regs[0] == 0U ||
        result->services_handled != 1U) {
        return fail("real libdl dlopen wrapper failed");
    }
    const std::uint32_t handle = result->regs[0];
    ++service_calls;

    result = run_wrapper(
        memory, link_map.graph, "fixture_dlsym", registry,
        stack_top, *stop, handle, symbol_name);
    if (!result || !*result ||
        result->regs[0] != provider_symbol.symbol.symbol.guest_value ||
        result->services_handled != 1U) {
        return fail("real libdl dlsym wrapper did not return provider address");
    }
    ++service_calls;

    result = run_wrapper(
        memory, link_map.graph, "fixture_dladdr", registry,
        stack_top, *stop,
        provider_symbol.symbol.symbol.guest_value,
        dl_info);
    if (!result || !*result || result->regs[0] != 1U ||
        result->services_handled != 1U) {
        return fail("real libdl dladdr wrapper failed");
    }
    ++service_calls;

    const std::uint32_t fname = read_u32_le(memory, dl_info);
    const std::uint32_t fbase = read_u32_le(memory, dl_info + 4U);
    const std::uint32_t sname = read_u32_le(memory, dl_info + 8U);
    const std::uint32_t saddr = read_u32_le(memory, dl_info + 12U);
    if (fname == 0U || sname == 0U ||
        fbase != link_map.graph.objects[*provider_index].load.load_bias ||
        saddr != provider_symbol.symbol.symbol.guest_value ||
        read_c_string(memory, fname, kMaxFixtureNameBytes) !=
            kProviderSoname ||
        read_c_string(memory, sname, kMaxFixtureNameBytes) !=
            "fixture_dynamic_value") {
        return fail("real libdl dladdr did not publish provider/symbol metadata");
    }

    result = run_wrapper(
        memory, link_map.graph, "fixture_dlclose", registry,
        stack_top, *stop, handle);
    if (!result || !*result || result->regs[0] != 0U ||
        result->services_handled != 1U ||
        read_u32_le(
            memory,
            close_marker_symbol.symbol.symbol.guest_value) != 0x0D1C105EU ||
        lifecycle.objects[*provider_index].destructors !=
            Elf32LifecycleObjectStatus::Complete) {
        std::string detail =
            "real libdl dlclose lifecycle transaction failed";
        if (result.has_value()) {
            detail += ": r0=" + std::to_string(result->regs[0]) +
                      ", services=" +
                      std::to_string(result->services_handled) +
                      ", marker=" +
                      std::to_string(read_u32_le(
                          memory,
                          close_marker_symbol.symbol.symbol.guest_value)) +
                      ", lifecycle=" +
                      std::to_string(static_cast<unsigned int>(
                          lifecycle.objects[*provider_index].destructors));

            const auto error_result = run_wrapper(
                memory, link_map.graph, "fixture_dlerror", registry,
                stack_top, *stop);
            if (error_result && *error_result &&
                error_result->regs[0] != 0U) {
                const std::string error_text = read_c_string(
                    memory, error_result->regs[0], 127U);
                if (!error_text.empty()) {
                    detail += ", dlerror=" + error_text;
                }
            }
        }
        return fail(detail);
    }
    ++service_calls;

    result = run_wrapper(
        memory, link_map.graph, "fixture_dlsym", registry,
        stack_top, *stop, handle, symbol_name);
    if (!result || !*result || result->regs[0] != 0U ||
        result->services_handled != 1U) {
        return fail("closed libdl handle did not reject dlsym");
    }
    ++service_calls;

    result = run_wrapper(
        memory, link_map.graph, "fixture_dlerror", registry,
        stack_top, *stop);
    if (!result || !*result || result->regs[0] == 0U ||
        result->services_handled != 1U ||
        read_c_string(memory, result->regs[0], 127U).empty()) {
        return fail("real libdl dlerror wrapper did not publish pending error");
    }
    ++service_calls;

    std::cout
        << "fixture.libdl.object_count=" << link_map.graph.objects.size() << '\n'
        << "fixture.libdl.needed=" << kA32LibDlShimSoname << '\n'
        << "fixture.libdl.namespace_access=linked\n"
        << "fixture.libdl.symbol_object=" << *shim_index << '\n'
        << "fixture.libdl.provider_object=" << *provider_index << '\n'
        << "fixture.libdl.fini_marker=0x0d1c105e\n"
        << "fixture.libdl.service_calls=" << service_calls << '\n'
        << "fixture.libdl.status=PASS\n";
    return 0;
}