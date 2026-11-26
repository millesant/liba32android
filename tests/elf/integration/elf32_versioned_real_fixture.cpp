#include <cstdint>
#include <iostream>
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
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_plt_rel_relocations;
using liba32android::elf::build_elf32_plt_rel_relocation_plan;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol_for_reference;
using liba32android::memory::MappedGuestMemory;

constexpr const char* kProviderSoname =
    "liba32android_versioned_provider.so";
constexpr const char* kImportedSymbol = "fixture_versioned_import";
constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 12U << 20;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class FixtureProvider final : public Elf32DependencyProvider {
public:
    explicit FixtureProvider(std::vector<std::uint8_t> image)
        : image_(std::move(image)) {}

    std::size_t calls{};

    Elf32DependencyProviderResult resolve(
        std::string_view requested_name,
        std::uint64_t max_image_bytes) override {
        ++calls;
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
        result.source.identity = "real-versioned-provider";
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
        .max_version_records = 64,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16;
    result.symbols = symbol_options();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return fail(
            "expected paths to generated ARM32 versioned consumer and provider");
    }

    const auto consumer =
        liba32android::test_support::read_binary_file(argv[1]);
    const auto provider_image =
        liba32android::test_support::read_binary_file(argv[2]);
    if (consumer.empty() || provider_image.empty()) {
        return fail("generated ARM32 versioned fixture is missing or empty");
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
            .identity = "real-versioned-consumer",
            .image = consumer,
        },
        provider,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("real versioned dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 2 || provider.calls != 1) {
        return fail("real versioned fixture did not produce a two-object graph");
    }

    const auto& requester = graph_result.graph.objects[0];
    const auto& definition = graph_result.graph.objects[1];
    if (!requester.linker_metadata.version_symbol_table.has_value() ||
        !requester.linker_metadata.version_requirement_table.has_value() ||
        !definition.linker_metadata.version_symbol_table.has_value() ||
        !definition.linker_metadata.version_definition_table.has_value() ||
        !definition.linker_strings.soname.has_value() ||
        *definition.linker_strings.soname != kProviderSoname) {
        return fail("real versioned fixture metadata was incomplete");
    }

    const auto opts = relocation_options();
    const auto plan = build_elf32_plt_rel_relocation_plan(
        memory, graph_result.graph, 0, opts);
    if (!plan || plan.plan.entries.size() != 1 ||
        plan.plan.entries[0].type != kRArmJumpSlot ||
        plan.plan.entries[0].symbol_index == 0) {
        return fail("real versioned consumer did not expose one JUMP_SLOT");
    }

    const auto independent = lookup_elf32_graph_symbol_for_reference(
        memory, graph_result.graph, 0,
        plan.plan.entries[0].symbol_index, kImportedSymbol, symbol_options());
    if (!independent || independent.symbol.object_index != 1) {
        return fail("version-aware graph lookup did not select provider");
    }

    const auto applied = apply_elf32_plt_rel_relocations(
        memory, graph_result.graph, 0, opts);
    if (!applied || applied.application.writes.size() != 1 ||
        applied.application.writes[0].final_word !=
            independent.symbol.symbol.guest_value) {
        return fail("versioned JUMP_SLOT relocation did not apply provider value");
    }

    std::uint32_t target_value = 0;
    if (!liba32android::test_support::read_u32_le(
            memory,
            applied.application.writes[0].place_guest_address,
            target_value) ||
        target_value != independent.symbol.symbol.guest_value) {
        return fail("versioned JUMP_SLOT target did not contain provider value");
    }

    std::cout << "fixture.versioned.object_count="
              << graph_result.graph.objects.size() << '\n'
              << "fixture.versioned.version_name=LIBC\n"
              << "fixture.versioned.relocation_count="
              << applied.application.writes.size() << '\n'
              << "fixture.versioned.provider_calls=" << provider.calls << '\n'
              << "fixture.versioned.status=PASS\n";
    return 0;
}
