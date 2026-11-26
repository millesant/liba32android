#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32LoadedSegment;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::build_elf32_symbol_index;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::memory::has_permission;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::uint32_t kExpectedFixtureData = 0x12345678U;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class FailIfCalledProvider final : public Elf32DependencyProvider {
public:
    std::size_t calls{};

    Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        ++calls;
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::Failed;
        return result;
    }
};

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 256,
        .max_hash_buckets = 256,
        .max_gnu_bloom_words = 64,
        .max_scope_objects = 8,
        .max_name_bytes = kMaxFixtureNameBytes,
    };
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("real ARM32 fixture symbol lookup failed for ") +
           std::string(name) + ": graph=" +
           liba32android::elf::to_string(result.error) + ", index=" +
           liba32android::elf::to_string(result.index_error) + ", lookup=" +
           liba32android::elf::to_string(result.lookup_error) + ", string=" +
           liba32android::elf::to_string(result.string_error);
}

bool lies_in_executable_segment(
    const std::vector<Elf32LoadedSegment>& segments,
    std::uint32_t guest_value) {
    for (const auto& segment : segments) {
        const std::uint64_t start = segment.guest_address;
        const std::uint64_t end = start + segment.memory_size;
        if (guest_value >= start && guest_value < end &&
            has_permission(segment.permissions, MemoryPermission::Execute)) {
            return true;
        }
    }
    return false;
}

struct SegmentSnapshot {
    std::uint32_t guest_address{};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_size{};
    MemoryPermission permissions{MemoryPermission::None};
    std::vector<std::uint8_t> bytes;
};

bool snapshot_segments(
    const MappedGuestMemory& memory,
    const std::vector<Elf32LoadedSegment>& segments,
    std::vector<SegmentSnapshot>& snapshots) {
    snapshots.clear();
    snapshots.reserve(segments.size());
    for (const auto& segment : segments) {
        if (!has_permission(segment.permissions, MemoryPermission::Read)) {
            return false;
        }

        SegmentSnapshot snapshot;
        snapshot.guest_address = segment.guest_address;
        snapshot.mapping_start = segment.mapping_start;
        snapshot.mapping_size = segment.mapping_size;
        snapshot.permissions = segment.permissions;
        snapshot.bytes.resize(segment.memory_size);
        if (!memory.read(segment.guest_address, snapshot.bytes)) {
            return false;
        }
        snapshots.push_back(std::move(snapshot));
    }
    return true;
}

bool snapshots_unchanged(
    const MappedGuestMemory& memory,
    const std::vector<SegmentSnapshot>& snapshots) {
    const std::uint64_t page_size = memory.page_size();
    for (const auto& snapshot : snapshots) {
        const std::uint64_t mapping_end =
            static_cast<std::uint64_t>(snapshot.mapping_start) +
            snapshot.mapping_size;
        for (std::uint64_t page = snapshot.mapping_start;
             page < mapping_end;
             page += page_size) {
            if (page > std::numeric_limits<std::uint32_t>::max()) return false;
            const auto guest_page = static_cast<std::uint32_t>(page);
            if (!memory.is_mapped(guest_page) ||
                memory.permissions(guest_page) != snapshot.permissions) {
                return false;
            }
        }

        std::vector<std::uint8_t> after(snapshot.bytes.size());
        if (!memory.read(snapshot.guest_address, after) ||
            after != snapshot.bytes) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected path to generated ARM32 fixture");
    }

    const std::vector<std::uint8_t> image = liba32android::test_support::read_binary_file(argv[1]);
    if (image.empty()) {
        return fail("generated ARM32 fixture is missing or empty");
    }

    MappedGuestMemory memory;
    FailIfCalledProvider provider;

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
            .identity = "real-arm32-fixture",
            .image = image,
        },
        provider,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("real ARM32 fixture dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 1 || provider.calls != 0) {
        return fail("real ARM32 fixture did not remain a one-object zero-dependency graph");
    }

    const auto& root = graph_result.graph.objects.front();
    const Elf32SymbolLookupOptions lookup_options = symbol_options();
    const auto index = build_elf32_symbol_index(
        memory, root.linker_metadata, lookup_options);
    if (!index) {
        return fail(
            std::string("real ARM32 fixture GNU symbol index failed: ") +
            liba32android::elf::to_string(index.error));
    }
    if (!index.index.gnu_hash.has_value()) {
        return fail("real ARM32 fixture did not expose the expected DT_GNU_HASH index");
    }

    std::vector<SegmentSnapshot> before;
    if (!snapshot_segments(memory, root.load.segments, before)) {
        return fail("could not snapshot loaded real ARM32 fixture before symbol lookup");
    }

    const auto add = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_add", lookup_options);
    const auto data = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_data", lookup_options);
    const auto bss = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_bss", lookup_options);

    if (!add) return fail(lookup_error("fixture_add", add));
    if (!data) return fail(lookup_error("fixture_data", data));
    if (!bss) return fail(lookup_error("fixture_bss", bss));

    if (add.symbol.object_index != 0 ||
        data.symbol.object_index != 0 ||
        bss.symbol.object_index != 0) {
        return fail("real ARM32 fixture symbols did not resolve from graph object 0");
    }
    if (add.symbol.symbol.name != "fixture_add" ||
        data.symbol.symbol.name != "fixture_data" ||
        bss.symbol.symbol.name != "fixture_bss") {
        return fail("real ARM32 fixture symbol lookup returned an unexpected exact name");
    }

    std::uint32_t data_value = 0;
    if (!liba32android::test_support::read_u32_le(memory, data.symbol.symbol.guest_value, data_value) ||
        data_value != kExpectedFixtureData) {
        return fail("resolved fixture_data did not read back 0x12345678");
    }

    std::uint32_t bss_value = std::numeric_limits<std::uint32_t>::max();
    if (!liba32android::test_support::read_u32_le(memory, bss.symbol.symbol.guest_value, bss_value) ||
        bss_value != 0) {
        return fail("resolved fixture_bss did not read back zero");
    }

    if (!lies_in_executable_segment(
            root.load.segments, add.symbol.symbol.guest_value)) {
        return fail("resolved fixture_add value was outside executable loaded segments");
    }

    if (!snapshots_unchanged(memory, before)) {
        return fail("real ARM32 fixture symbol lookup mutated loaded guest bytes or mappings");
    }

    std::cout << "fixture.symbol.gnu_hash=present\n"
              << "fixture.symbol.symbol_count=" << index.index.symbol_count << '\n'
              << "fixture.symbol.fixture_add=0x" << std::hex
              << add.symbol.symbol.guest_value << '\n'
              << "fixture.symbol.fixture_data=0x"
              << data.symbol.symbol.guest_value << '\n'
              << "fixture.symbol.fixture_bss=0x"
              << bss.symbol.symbol.guest_value << std::dec << '\n'
              << "fixture.symbol.fixture_data_value=0x" << std::hex
              << data_value << std::dec << '\n'
              << "fixture.symbol.fixture_bss_value=" << bss_value << '\n'
              << "fixture.symbol.status=PASS\n";
    return 0;
}
