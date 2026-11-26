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
using liba32android::elf::apply_elf32_plt_rel_relocations;
using liba32android::elf::build_elf32_plt_rel_relocation_plan;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::elf::resolve_elf32_plt_rel_relocation_references;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;
using liba32android::memory::has_permission;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 12U << 20;
constexpr const char* kProviderSoname =
    "liba32android_jump_slot_provider.so";
constexpr const char* kImportedSymbol = "fixture_import";

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class FixtureProvider final : public Elf32DependencyProvider {
public:
    explicit FixtureProvider(std::vector<std::uint8_t> image)
        : image_(std::move(image)) {}

    std::size_t calls{};
    std::string last_requested;

    Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        ++calls;
        last_requested.assign(requested_name);
        if (requested_name != kProviderSoname) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::NotFound;
            return result;
        }
        if (image_.size() > max_image_bytes) {
            Elf32DependencyProviderResult result;
            result.error = Elf32DependencyProviderError::Failed;
            return result;
        }

        Elf32DependencyProviderResult result;
        result.source.identity = "real-jump-slot-provider";
        result.source.image = image_;
        return result;
    }

private:
    std::vector<std::uint8_t> image_;
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
    return std::string("real JUMP_SLOT symbol lookup failed for ") +
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
    const std::vector<liba32android::elf::Elf32LoadedDependencyObject>& objects,
    std::vector<SegmentSnapshot>& snapshots) {
    snapshots.clear();
    for (const auto& object : objects) {
        for (const Elf32LoadedSegment& segment : object.load.segments) {
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
    }
    return true;
}

bool allowed_target_byte(
    std::uint64_t address,
    const std::vector<std::uint32_t>& targets) {
    for (const std::uint32_t target : targets) {
        const std::uint64_t begin = target;
        if (address >= begin && address < begin + 4U) return true;
    }
    return false;
}

bool snapshots_unchanged_except_targets(
    const MappedGuestMemory& memory,
    const std::vector<SegmentSnapshot>& snapshots,
    const std::vector<std::uint32_t>& targets) {
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
            if (!allowed_target_byte(address, targets) &&
                after[i] != snapshot.bytes[i]) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail(
            "expected paths to generated ARM32 JUMP_SLOT consumer and provider");
    }

    const std::vector<std::uint8_t> consumer = liba32android::test_support::read_binary_file(argv[1]);
    const std::vector<std::uint8_t> provider_image = liba32android::test_support::read_binary_file(argv[2]);
    if (consumer.empty() || provider_image.empty()) {
        return fail("generated ARM32 JUMP_SLOT fixture is missing or empty");
    }

    MappedGuestMemory memory;
    FixtureProvider provider(provider_image);

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
            .identity = "real-jump-slot-consumer",
            .image = consumer,
        },
        provider,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("real JUMP_SLOT dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error) +
            ", dependency=" +
            liba32android::elf::to_string(graph_result.dependency_error));
    }

    if (graph_result.graph.objects.size() != 2 ||
        provider.calls != 1 ||
        provider.last_requested != kProviderSoname) {
        return fail("real JUMP_SLOT fixture did not produce the expected two-object graph");
    }

    const auto& consumer_object = graph_result.graph.objects[0];
    const auto& provider_object = graph_result.graph.objects[1];
    if (consumer_object.identity != "real-jump-slot-consumer" ||
        provider_object.identity != "real-jump-slot-provider" ||
        consumer_object.linker_strings.needed.size() != 1 ||
        consumer_object.linker_strings.needed[0] != kProviderSoname ||
        consumer_object.dependencies.size() != 1 ||
        consumer_object.dependencies[0].requested_name != kProviderSoname ||
        consumer_object.dependencies[0].target_object != 1) {
        return fail("real JUMP_SLOT dependency edge metadata was incorrect");
    }
    if (!consumer_object.linker_metadata.plt_rel_table.has_value()) {
        return fail("real JUMP_SLOT consumer did not expose validated PLT REL metadata");
    }

    const auto options = relocation_options();
    const auto plan = build_elf32_plt_rel_relocation_plan(
        memory, graph_result.graph, 0, options);
    if (!plan || plan.plan.entries.size() != 1 ||
        plan.plan.entries[0].type != kRArmJumpSlot ||
        plan.plan.entries[0].symbol_index == 0 ||
        !plan.plan.entries[0].original_word.has_value()) {
        return fail("real JUMP_SLOT consumer did not produce one valid PLT plan entry");
    }

    const auto resolved = resolve_elf32_plt_rel_relocation_references(
        memory, graph_result.graph, 0, options);
    if (!resolved || resolved.resolution.entries.size() != 1 ||
        !resolved.resolution.entries[0].reference.has_value()) {
        return fail("real JUMP_SLOT reference did not resolve");
    }
    const auto& reference = *resolved.resolution.entries[0].reference;
    if (reference.name != kImportedSymbol ||
        reference.unresolved_weak ||
        !reference.defining_object_index.has_value() ||
        *reference.defining_object_index != 1) {
        return fail("real JUMP_SLOT reference did not select the provider object");
    }

    const auto independent = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, kImportedSymbol, symbol_options());
    if (!independent) return fail(lookup_error(kImportedSymbol, independent));
    if (independent.symbol.object_index != 1 ||
        independent.symbol.symbol.guest_value != reference.symbol_value) {
        return fail("independent graph lookup disagreed with JUMP_SLOT resolution");
    }

    const std::uint32_t target =
        plan.plan.entries[0].place_guest_address;
    const std::uint32_t original_word =
        *plan.plan.entries[0].original_word;
    std::uint32_t target_before = 0;
    if (!liba32android::test_support::read_u32_le(memory, target, target_before) ||
        target_before != original_word) {
        return fail("real JUMP_SLOT target could not be read before application");
    }

    std::vector<SegmentSnapshot> before;
    if (!snapshot_segments(memory, graph_result.graph.objects, before)) {
        return fail("could not snapshot real JUMP_SLOT graph before application");
    }

    const auto applied = apply_elf32_plt_rel_relocations(
        memory, graph_result.graph, 0, options);
    if (!applied) {
        return fail(
            std::string("real JUMP_SLOT application failed: ") +
            liba32android::elf::to_string(applied.error) +
            ", primary=" +
            liba32android::elf::to_string(applied.primary_error));
    }
    if (applied.application.writes.size() != 1 ||
        applied.application.writes[0].type != kRArmJumpSlot ||
        applied.application.writes[0].place_guest_address != target ||
        applied.application.writes[0].original_word != original_word ||
        applied.application.writes[0].final_word !=
            independent.symbol.symbol.guest_value) {
        return fail("real JUMP_SLOT application record was incorrect");
    }

    std::uint32_t target_after = 0;
    if (!liba32android::test_support::read_u32_le(memory, target, target_after) ||
        target_after != independent.symbol.symbol.guest_value) {
        return fail("real JUMP_SLOT target did not equal provider guest symbol value");
    }
    if (provider.calls != 1) {
        return fail("JUMP_SLOT application unexpectedly reacquired dependencies");
    }

    const std::vector<std::uint32_t> allowed_targets{target};
    if (!snapshots_unchanged_except_targets(
            memory, before, allowed_targets)) {
        return fail("JUMP_SLOT changed non-target bytes or mapping permissions");
    }

    std::cout << "fixture.jump_slot.object_count="
              << graph_result.graph.objects.size() << '\n'
              << "fixture.jump_slot.needed=" << kProviderSoname << '\n'
              << "fixture.jump_slot.relocation_count="
              << plan.plan.entries.size() << '\n'
              << "fixture.jump_slot.target=0x" << std::hex << target << '\n'
              << "fixture.jump_slot.original_word=0x" << original_word << '\n'
              << "fixture.jump_slot.symbol_value=0x"
              << independent.symbol.symbol.guest_value << std::dec << '\n'
              << "fixture.jump_slot.provider_calls=" << provider.calls << '\n'
              << "fixture.jump_slot.mappings_unchanged=true\n"
              << "fixture.jump_slot.status=PASS\n";
    return 0;
}
