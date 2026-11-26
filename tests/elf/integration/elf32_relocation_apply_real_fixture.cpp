#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
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
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_rel_relocations;
using liba32android::elf::kRArmGlobDat;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::memory::has_permission;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::uint32_t kBssRelOffset = 0x82cc;
constexpr std::uint32_t kDataRelOffset = 0x82d0;
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

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16;
    result.symbols = symbol_options();
    return result;
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

bool allowed_relocation_byte(std::uint64_t address,
                             std::uint32_t bss_target,
                             std::uint32_t data_target) {
    const std::uint64_t bss = bss_target;
    const std::uint64_t data = data_target;
    return (address >= bss && address < bss + 4U) ||
           (address >= data && address < data + 4U);
}

bool snapshots_unchanged_except_targets(
    const MappedGuestMemory& memory,
    const std::vector<SegmentSnapshot>& snapshots,
    std::uint32_t bss_target,
    std::uint32_t data_target) {
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
        if (!memory.read(snapshot.guest_address, after)) return false;
        for (std::size_t i = 0; i < after.size(); ++i) {
            const std::uint64_t address =
                static_cast<std::uint64_t>(snapshot.guest_address) + i;
            if (!allowed_relocation_byte(address, bss_target, data_target) &&
                after[i] != snapshot.bytes[i]) {
                return false;
            }
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
        return fail("real fixture did not remain a one-object zero-dependency graph");
    }

    const auto& root = graph_result.graph.objects.front();
    const auto lookup_options = symbol_options();
    const auto data = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_data", lookup_options);
    const auto bss = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_bss", lookup_options);
    if (!data) return fail(lookup_error("fixture_data", data));
    if (!bss) return fail(lookup_error("fixture_bss", bss));
    if (data.symbol.object_index != 0 || bss.symbol.object_index != 0) {
        return fail("fixture relocation definitions were not in graph object 0");
    }

    std::uint32_t data_value_before = 0;
    std::uint32_t bss_value_before = std::numeric_limits<std::uint32_t>::max();
    if (!liba32android::test_support::read_u32_le(memory, data.symbol.symbol.guest_value, data_value_before) ||
        data_value_before != kExpectedFixtureData ||
        !liba32android::test_support::read_u32_le(memory, bss.symbol.symbol.guest_value, bss_value_before) ||
        bss_value_before != 0) {
        return fail("fixture data/BSS pre-relocation values were unexpected");
    }

    const std::uint32_t load_bias = root.load.load_bias;
    const std::uint32_t bss_target = load_bias + kBssRelOffset;
    const std::uint32_t data_target = load_bias + kDataRelOffset;
    std::uint32_t target = 1;
    if (!liba32android::test_support::read_u32_le(memory, bss_target, target) || target != 0 ||
        !liba32android::test_support::read_u32_le(memory, data_target, target) || target != 0) {
        return fail("fixture GLOB_DAT targets were not initially zero");
    }

    std::vector<SegmentSnapshot> before;
    if (!snapshot_segments(memory, root.load.segments, before)) {
        return fail("could not snapshot fixture segments before relocation");
    }

    const auto applied = apply_elf32_rel_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!applied) {
        return fail(
            std::string("real ARM32 fixture relocation application failed: ") +
            liba32android::elf::to_string(applied.error) +
            ", primary=" +
            liba32android::elf::to_string(applied.primary_error));
    }
    if (applied.application.writes.size() != 2 ||
        applied.application.writes[0].type != kRArmGlobDat ||
        applied.application.writes[1].type != kRArmGlobDat ||
        applied.application.writes[0].place_guest_address != bss_target ||
        applied.application.writes[1].place_guest_address != data_target ||
        applied.application.writes[0].final_word !=
            bss.symbol.symbol.guest_value ||
        applied.application.writes[1].final_word !=
            data.symbol.symbol.guest_value) {
        return fail("real fixture relocation application records were incorrect");
    }

    std::uint32_t bss_target_value = 0;
    std::uint32_t data_target_value = 0;
    if (!liba32android::test_support::read_u32_le(memory, bss_target, bss_target_value) ||
        bss_target_value != bss.symbol.symbol.guest_value ||
        !liba32android::test_support::read_u32_le(memory, data_target, data_target_value) ||
        data_target_value != data.symbol.symbol.guest_value) {
        return fail("real fixture GLOB_DAT targets did not equal resolved guest values");
    }

    std::uint32_t data_value_after = 0;
    std::uint32_t bss_value_after = std::numeric_limits<std::uint32_t>::max();
    if (!liba32android::test_support::read_u32_le(memory, data.symbol.symbol.guest_value, data_value_after) ||
        data_value_after != kExpectedFixtureData ||
        !liba32android::test_support::read_u32_le(memory, bss.symbol.symbol.guest_value, bss_value_after) ||
        bss_value_after != 0) {
        return fail("real fixture data/BSS changed after relocation");
    }
    if (provider.calls != 0) {
        return fail("relocation application unexpectedly called dependency provider");
    }
    if (!snapshots_unchanged_except_targets(
            memory, before, bss_target, data_target)) {
        return fail("relocation changed bytes outside targets or mapping permissions");
    }

    std::cout << "fixture.relocation_apply.count=2\n"
              << "fixture.relocation_apply.bss_target=0x" << std::hex
              << bss_target << '\n'
              << "fixture.relocation_apply.bss_value=0x"
              << bss_target_value << '\n'
              << "fixture.relocation_apply.data_target=0x"
              << data_target << '\n'
              << "fixture.relocation_apply.data_value=0x"
              << data_target_value << std::dec << '\n'
              << "fixture.relocation_apply.provider_calls=" << provider.calls << '\n'
              << "fixture.relocation_apply.mappings_unchanged=true\n"
              << "fixture.relocation_apply.data_bss_unchanged=true\n"
              << "fixture.relocation_apply.status=PASS\n";
    return 0;
}
