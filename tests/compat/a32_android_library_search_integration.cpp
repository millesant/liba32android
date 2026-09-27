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

#include "compat/a32_android_library_search.h"
#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::compat::A32AndroidLibrarySearchOptions;
using liba32android::compat::A32AndroidLibrarySearchProvider;
using liba32android::compat::A32AndroidLibrarySearchRoot;
using liba32android::compat::A32AndroidLibrarySource;
using liba32android::compat::A32AndroidLibrarySourceError;
using liba32android::compat::A32AndroidLibrarySourceResult;
using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::execute;
using liba32android::cpu::InstructionSet;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::string_view kRootIdentity = "libfixture_app_root.so";
constexpr std::string_view kChildSoname = "libfixture_app_child.so";
constexpr std::string_view kChildIdentity =
    "apk:base.apk!/lib/armeabi-v7a/libfixture_app_child.so";
constexpr std::string_view kSearchRoot =
    "base.apk!/lib/armeabi-v7a";
constexpr std::uint32_t kMaxNameBytes = 128U;
constexpr std::uint64_t kMaxImageBytes = 4U << 20;
constexpr std::size_t kInstructionBudget = 256U;
constexpr std::size_t kStackPages = 4U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class SingleImageSource final : public A32AndroidLibrarySource {
public:
    explicit SingleImageSource(
        std::span<const std::uint8_t> image) noexcept
        : image_(image) {}

    std::size_t calls{};
    std::string last_path;
    std::uint64_t last_ceiling{};

    A32AndroidLibrarySourceResult load(
        std::string_view path,
        std::uint64_t max_image_bytes) override {
        ++calls;
        last_path.assign(path.data(), path.size());
        last_ceiling = max_image_bytes;

        A32AndroidLibrarySourceResult result;
        if (path !=
            "base.apk!/lib/armeabi-v7a/libfixture_app_child.so") {
            result.error = A32AndroidLibrarySourceError::NotFound;
            return result;
        }
        if (image_.empty() || image_.size() > max_image_bytes) {
            result.error = A32AndroidLibrarySourceError::Failed;
            return result;
        }
        result.identity.assign(
            kChildIdentity.data(), kChildIdentity.size());
        result.image.assign(image_.begin(), image_.end());
        return result;
    }

private:
    std::span<const std::uint8_t> image_;
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

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16U;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("Android app-search symbol lookup failed for ") +
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
    if (argc != 3) {
        return fail("expected ARM32 app-search root and child fixture paths");
    }

    const std::vector<std::uint8_t> root_image =
        liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> child_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (root_image.empty() || child_image.empty()) {
        return fail("generated Android app-search fixture is missing or empty");
    }

    SingleImageSource source{std::span<const std::uint8_t>{child_image}};
    const std::array<A32AndroidLibrarySearchRoot, 1> roots{{
        {kRootIdentity, kSearchRoot},
    }};
    A32AndroidLibrarySearchProvider provider{
        std::span{roots},
        source,
        A32AndroidLibrarySearchOptions{
            .max_path_bytes = 256U,
        },
    };

    MappedGuestMemory memory;
    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 4U;
    load_options.max_depth = 4U;
    load_options.max_dependency_occurrences = 4U;
    load_options.max_image_bytes = kMaxImageBytes;
    load_options.max_total_image_bytes = 8U << 20;
    load_options.max_string_bytes = kMaxNameBytes;

    const auto loaded = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = std::string{kRootIdentity},
            .image = root_image,
        },
        provider,
        load_options);
    if (!loaded || loaded.graph.objects.size() != 2U) {
        return fail("Android app-search provider did not form two-object graph");
    }

    const auto& root = loaded.graph.objects[0];
    const auto& child = loaded.graph.objects[1];
    if (root.dependencies.size() != 1U ||
        root.dependencies[0].requested_name != kChildSoname ||
        root.dependencies[0].target_object != 1U ||
        child.identity != kChildIdentity ||
        source.calls != 1U ||
        source.last_path !=
            "base.apk!/lib/armeabi-v7a/libfixture_app_child.so" ||
        source.last_ceiling != kMaxImageBytes) {
        return fail("Android app-search requester/path metadata was incorrect");
    }

    const auto call = lookup_elf32_graph_symbol(
        memory, loaded.graph, 0U, "fixture_app_root_call", symbol_options());
    const auto child_value = lookup_elf32_graph_symbol(
        memory, loaded.graph, 0U, "fixture_app_child_value", symbol_options());
    if (!call) {
        return fail(lookup_error("fixture_app_root_call", call));
    }
    if (!child_value) {
        return fail(lookup_error("fixture_app_child_value", child_value));
    }
    if (call.symbol.object_index != 0U ||
        child_value.symbol.object_index != 1U) {
        return fail("Android app-search fixture symbols resolved from wrong objects");
    }

    const auto relocated = apply_elf32_combined_relocations(
        memory, loaded.graph, 0U, relocation_options());
    if (!relocated ||
        relocated.application.writes.size() != 1U ||
        relocated.application.writes[0].final_word !=
            child_value.symbol.symbol.guest_value) {
        return fail("Android app-search root JUMP_SLOT relocation failed");
    }

    const auto stack = find_unmapped_region(
        memory, 0x71000000U, kStackPages);
    const auto stop = find_unmapped_region(memory, 0x72000000U, 1U);
    if (!stack.has_value() || !stop.has_value()) {
        return fail("could not reserve Android app-search execution harness");
    }
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    if (!memory.map(*stack, memory.page_size() * kStackPages, rw)) {
        return fail("could not map Android app-search stack");
    }

    const auto& symbol = call.symbol.symbol;
    const bool thumb = (symbol.symbol.value & 1U) != 0U;
    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) +
        memory.page_size() * kStackPages - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("Android app-search stack top overflowed");
    }

    ExecutionRequest request{};
    request.instruction_set =
        thumb ? InstructionSet::Thumb : InstructionSet::Arm;
    request.entry_pc = symbol.guest_value & ~1U;
    request.regs[13] =
        static_cast<std::uint32_t>(stack_top64) & ~7U;
    request.regs[14] = *stop | (thumb ? 1U : 0U);
    request.instruction_count = kInstructionBudget;
    request.stop_pc = *stop;

    const auto execution = execute(memory, request);
    if (execution.exception_raised ||
        execution.memory_fault ||
        !execution.stop_pc_reached ||
        execution.regs[0] != 123U) {
        return fail("Android app-search real ARM32 dependency did not execute");
    }

    std::cout
        << "fixture.android_search.object_count="
        << loaded.graph.objects.size() << '\n'
        << "fixture.android_search.requester=" << kRootIdentity << '\n'
        << "fixture.android_search.requested=" << kChildSoname << '\n'
        << "fixture.android_search.virtual_path=" << source.last_path << '\n'
        << "fixture.android_search.child_identity=" << child.identity << '\n'
        << "fixture.android_search.relocation_count="
        << relocated.application.writes.size() << '\n'
        << "fixture.android_search.result=" << execution.regs[0] << '\n'
        << "fixture.android_search.status=PASS\n";
    return 0;
}
