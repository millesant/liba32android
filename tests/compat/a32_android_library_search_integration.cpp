#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "compat/a32_android_apk_runtime.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::compat::A32AndroidApkRuntimeBootstrap;
using liba32android::compat::A32AndroidApkRuntimeBootstrapOptions;
using liba32android::compat::A32ApkLibrarySourceOptions;
using liba32android::compat::A32LibDlHandle;
using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::cpu::execute;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::string_view kRootSoname = "libfixture_app_root.so";
constexpr std::string_view kChildSoname = "libfixture_app_child.so";
constexpr std::string_view kAbiDirectory = "lib/armeabi-v7a";
constexpr std::uint32_t kMaxNameBytes = 128U;
constexpr std::uint64_t kMaxImageBytes = 4U << 20;
constexpr std::size_t kInstructionBudget = 256U;
constexpr std::size_t kStackPages = 4U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class NotFoundPlatformProvider final : public Elf32DependencyProvider {
public:
    Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::NotFound;
        return result;
    }
};

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 128U,
        .max_hash_buckets = 128U,
        .max_gnu_bloom_words = 64U,
        .max_scope_objects = 4U,
        .max_name_bytes = kMaxNameBytes,
        .max_version_records = 64U,
    };
}

A32AndroidApkRuntimeBootstrapOptions bootstrap_options(
    std::uint32_t stack_top,
    std::uint32_t return_pc) {
    A32AndroidApkRuntimeBootstrapOptions result;
    result.max_application_libraries = 4U;
    result.max_soname_bytes = kMaxNameBytes;
    result.max_abi_directory_bytes = 64U;
    result.source = A32ApkLibrarySourceOptions{
        .max_virtual_path_bytes = 4096U,
        .max_archive_bytes = 8U << 20,
        .max_entries = 16U,
        .max_central_directory_bytes = 64U << 10,
        .max_entry_name_bytes = 256U,
    };
    result.search.max_path_bytes = 4096U;

    result.open.handle_base = 0x70000000U;
    result.open.load.max_objects = 4U;
    result.open.load.max_depth = 4U;
    result.open.load.max_dependency_occurrences = 8U;
    result.open.load.max_image_bytes = kMaxImageBytes;
    result.open.load.max_total_image_bytes = 8U << 20;
    result.open.load.max_string_bytes = kMaxNameBytes;
    result.open.load.placement.search_begin = 0x10000U;
    result.open.load.placement.search_end_exclusive = 0x60000000U;

    result.open.relocation.max_relocations = 16U;
    result.open.relocation.symbols = symbol_options();

    result.open.relro.max_pages = 16U;
    result.open.lifecycle.max_objects = 4U;
    result.open.lifecycle.max_array_entries = 16U;
    result.open.lifecycle.execution.stack_top = stack_top;
    result.open.lifecycle.execution.return_pc = return_pc;
    result.open.lifecycle.execution.max_instructions_per_call =
        kInstructionBudget;

    result.open.reclamation.max_objects = 4U;
    result.open.reclamation.max_segments = 16U;
    result.open.reclamation.max_snapshot_bytes = 8U << 20;
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("Android APK bootstrap symbol lookup failed for ") +
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected deterministic ARM32 app-search APK path");
    }

    const std::filesystem::path apk_path{argv[1]};
    if (apk_path.empty()) {
        return fail("generated Android app-search APK path is empty");
    }

    MappedGuestMemory memory;
    const auto stack = find_unmapped_region(
        memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(
        memory, 0x72000000U, 1U);
    if (!stack.has_value() || !stop.has_value()) {
        return fail("could not reserve Android APK bootstrap harness");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*stack, memory.page_size() * kStackPages, rw)) {
        return fail("could not map Android APK bootstrap stack");
    }
    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("Android APK bootstrap stack top overflowed");
    }
    const std::uint32_t stack_top =
        static_cast<std::uint32_t>(stack_top64) & ~7U;

    Elf32LinkMap link_map;
    Elf32LifecycleState lifecycle;
    std::array<A32LibDlHandle, 4> handles{};
    NotFoundPlatformProvider platform;
    const std::array<std::string_view, 2> application_sonames{{
        kRootSoname,
        kChildSoname,
    }};

    A32AndroidApkRuntimeBootstrap bootstrap{
        memory,
        link_map,
        platform,
        std::span{handles},
        lifecycle,
        apk_path.string(),
        std::string{kAbiDirectory},
        std::span{application_sonames},
        bootstrap_options(stack_top, *stop),
    };
    if (!bootstrap.configuration_valid()) {
        return fail("Android APK bootstrap configuration was invalid");
    }

    const auto opened =
        bootstrap.open_root(kRootSoname, stack_top);
    if (!opened) {
        return fail(
            std::string("Android APK root bootstrap failed: bootstrap=") +
            liba32android::compat::to_string(opened.error) +
            ", open=" +
            liba32android::compat::to_string(
                opened.open_result.error));
    }
    if (!opened.open_result.object_index.has_value() ||
        *opened.open_result.object_index != 0U ||
        opened.open_result.guest_handle != 0x70000000U ||
        link_map.graph.objects.size() != 2U ||
        link_map.roots.size() != 1U ||
        lifecycle.objects.size() != 2U ||
        lifecycle.objects[0].constructors !=
            Elf32LifecycleObjectStatus::Complete ||
        lifecycle.objects[1].constructors !=
            Elf32LifecycleObjectStatus::Complete) {
        return fail("Android APK bootstrap did not publish initialized graph");
    }

    const std::string search_root =
        apk_path.string() + "!/" + std::string{kAbiDirectory};
    const std::string root_identity =
        search_root + "/" + std::string{kRootSoname};
    const std::string child_identity =
        search_root + "/" + std::string{kChildSoname};

    const auto& root = link_map.graph.objects[0];
    const auto& child = link_map.graph.objects[1];
    if (root.identity != root_identity ||
        child.identity != child_identity ||
        root.dependencies.size() != 1U ||
        root.dependencies[0].requested_name != kChildSoname ||
        root.dependencies[0].target_object != 1U) {
        return fail("Android APK bootstrap requester identities were incorrect");
    }

    const auto call = lookup_elf32_graph_symbol(
        memory,
        link_map.graph,
        0U,
        "fixture_app_root_call",
        symbol_options());
    const auto child_value = lookup_elf32_graph_symbol(
        memory,
        link_map.graph,
        0U,
        "fixture_app_child_value",
        symbol_options());
    if (!call) {
        return fail(lookup_error("fixture_app_root_call", call));
    }
    if (!child_value) {
        return fail(lookup_error("fixture_app_child_value", child_value));
    }
    if (call.symbol.object_index != 0U ||
        child_value.symbol.object_index != 1U) {
        return fail("Android APK bootstrap symbols resolved from wrong objects");
    }

    const auto reopened_child =
        bootstrap.open_transaction().open(
            kChildSoname, stack_top);
    if (!reopened_child ||
        !reopened_child.object_index.has_value() ||
        *reopened_child.object_index != 1U ||
        reopened_child.guest_handle != 0x70000004U ||
        handles[1].refcount != 1U) {
        return fail("persistent APK bootstrap transaction could not dlopen child");
    }

    const auto& symbol = call.symbol.symbol;
    const bool thumb = (symbol.symbol.value & 1U) != 0U;

    ExecutionRequest request{};
    request.instruction_set =
        thumb ? InstructionSet::Thumb : InstructionSet::Arm;
    request.entry_pc = symbol.guest_value & ~1U;
    request.regs[13] = stack_top;
    request.regs[14] = *stop | (thumb ? 1U : 0U);
    request.instruction_count = kInstructionBudget;
    request.stop_pc = *stop;

    const auto execution = execute(memory, request);
    if (execution.exception_raised ||
        execution.memory_fault ||
        !execution.stop_pc_reached ||
        execution.regs[0] != 123U) {
        return fail("Android APK bootstrap dependency did not execute");
    }

    std::cout
        << "fixture.android_search.object_count="
        << link_map.graph.objects.size() << '\n'
        << "fixture.android_search.requester=" << root.identity << '\n'
        << "fixture.android_search.requested=" << kChildSoname << '\n'
        << "fixture.android_search.source=apk-bootstrap\n"
        << "fixture.android_search.search_root=" << search_root << '\n'
        << "fixture.android_search.root_identity=" << root.identity << '\n'
        << "fixture.android_search.child_identity=" << child.identity << '\n'
        << "fixture.android_search.root_handle="
        << opened.open_result.guest_handle << '\n'
        << "fixture.android_search.child_handle="
        << reopened_child.guest_handle << '\n'
        << "fixture.android_search.result=" << execution.regs[0] << '\n'
        << "fixture.android_search.status=PASS\n";
    return 0;
}
