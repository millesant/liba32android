#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::build_elf32_rel_relocation_plan;
using liba32android::elf::kRArmGlobDat;
using liba32android::elf::resolve_elf32_rel_relocation_references;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::uint32_t kExpectedBssOffset = 0x82cc;
constexpr std::uint32_t kExpectedDataOffset = 0x82d0;
constexpr std::uint32_t kExpectedBssValue = 0xc2d8;
constexpr std::uint32_t kExpectedDataValue = 0xc2d4;

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
    load_options.max_string_bytes = 128;

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

    Elf32RelocationOptions relocation_options;
    relocation_options.max_relocations = 16;
    relocation_options.symbols.max_symbols = 64;
    relocation_options.symbols.max_hash_buckets = 64;
    relocation_options.symbols.max_gnu_bloom_words = 16;
    relocation_options.symbols.max_scope_objects = 8;
    relocation_options.symbols.max_name_bytes = 64;
    const auto plan = build_elf32_rel_relocation_plan(
        memory, graph_result.graph, 0, relocation_options);
    if (!plan) {
        return fail(
            std::string("real ARM32 fixture relocation plan failed: ") +
            liba32android::elf::to_string(plan.error));
    }
    if (plan.plan.entries.size() != 2) {
        return fail("real fixture did not expose exactly two main REL entries");
    }

    const auto& bss = plan.plan.entries[0];
    const auto& data = plan.plan.entries[1];
    const std::uint32_t load_bias =
        graph_result.graph.objects[0].load.load_bias;

    if (bss.type != kRArmGlobDat ||
        bss.symbol_index != 2 ||
        bss.offset != kExpectedBssOffset ||
        bss.place_guest_address != load_bias + kExpectedBssOffset ||
        !bss.original_word.has_value() ||
        *bss.original_word != 0) {
        return fail("real fixture first GLOB_DAT plan entry did not match pinned evidence");
    }
    if (data.type != kRArmGlobDat ||
        data.symbol_index != 3 ||
        data.offset != kExpectedDataOffset ||
        data.place_guest_address != load_bias + kExpectedDataOffset ||
        !data.original_word.has_value() ||
        *data.original_word != 0) {
        return fail("real fixture second GLOB_DAT plan entry did not match pinned evidence");
    }

    const auto resolution = resolve_elf32_rel_relocation_references(
        memory, graph_result.graph, 0, relocation_options);
    if (!resolution || resolution.resolution.entries.size() != 2 ||
        !resolution.resolution.entries[0].reference.has_value() ||
        !resolution.resolution.entries[1].reference.has_value()) {
        return fail(
            std::string("real ARM32 fixture relocation references failed: ") +
            liba32android::elf::to_string(resolution.error));
    }
    const auto& bss_reference = *resolution.resolution.entries[0].reference;
    const auto& data_reference = *resolution.resolution.entries[1].reference;
    if (bss_reference.name != "fixture_bss" ||
        bss_reference.symbol_value != load_bias + kExpectedBssValue ||
        bss_reference.unresolved_weak ||
        data_reference.name != "fixture_data" ||
        data_reference.symbol_value != load_bias + kExpectedDataValue ||
        data_reference.unresolved_weak) {
        return fail("real fixture relocation references resolved incorrectly");
    }

    std::array<std::uint8_t, 4> target_bytes{};
    if (!memory.read(load_bias + kExpectedBssOffset, target_bytes) ||
        target_bytes != std::array<std::uint8_t, 4>{0, 0, 0, 0} ||
        !memory.read(load_bias + kExpectedDataOffset, target_bytes) ||
        target_bytes != std::array<std::uint8_t, 4>{0, 0, 0, 0}) {
        return fail("real fixture read-only relocation resolution mutated targets");
    }

    std::cout << "fixture.relocation.count=2\n"
              << "fixture.relocation.0.type=R_ARM_GLOB_DAT\n"
              << "fixture.relocation.0.symbol_index=2\n"
              << "fixture.relocation.0.offset=0x" << std::hex
              << bss.offset << '\n'
              << "fixture.relocation.1.type=R_ARM_GLOB_DAT\n"
              << "fixture.relocation.1.symbol_index=3\n"
              << "fixture.relocation.1.offset=0x" << data.offset << std::dec
              << '\n'
              << "fixture.relocation.0.name=" << bss_reference.name << '\n'
              << "fixture.relocation.1.name=" << data_reference.name << '\n'
              << "fixture.relocation.references=PASS\n"
              << "fixture.relocation.status=PASS\n";
    return 0;
}
