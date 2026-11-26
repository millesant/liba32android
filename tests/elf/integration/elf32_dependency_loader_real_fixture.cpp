#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_load_plan.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32ImageType;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::plan_elf32_load;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint64_t kExpectedFixtureAlignment = 0x4000;
constexpr std::uint32_t kMaxFixtureNameBytes = 64;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr const char* kExpectedSoname = "liba32android_loader_fixture.so";

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
    const auto plan_result = plan_elf32_load(memory, image);
    if (!plan_result) {
        return fail(
            std::string("real ARM32 fixture load planning failed: ") +
            liba32android::elf::to_string(plan_result.error));
    }
    if (plan_result.plan.type != Elf32ImageType::Dynamic ||
        plan_result.plan.required_load_bias_alignment !=
            kExpectedFixtureAlignment) {
        return fail("real ARM32 fixture did not preserve ET_DYN / 0x4000 placement requirements");
    }

    FailIfCalledProvider provider;
    Elf32DependencyLoadOptions options;
    options.max_objects = 8;
    options.max_depth = 8;
    options.max_dependency_occurrences = 8;
    options.max_image_bytes = kMaxFixtureImageBytes;
    options.max_total_image_bytes = kMaxTotalImageBytes;
    options.max_string_bytes = kMaxFixtureNameBytes;

    const auto result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "real-arm32-fixture",
            .image = image,
        },
        provider,
        options);
    if (!result) {
        return fail(
            std::string("real ARM32 fixture dependency graph load failed: ") +
            liba32android::elf::to_string(result.error));
    }
    if (result.graph.objects.size() != 1) {
        return fail("zero-dependency real ARM32 fixture did not produce exactly one graph object");
    }

    const auto& root = result.graph.objects.front();
    if (root.identity != "real-arm32-fixture" ||
        !root.load.dynamic_segment.has_value()) {
        return fail("real ARM32 fixture graph root metadata was incomplete");
    }
    if ((root.load.load_bias % kExpectedFixtureAlignment) != 0) {
        return fail("real ARM32 fixture graph load violated p_align=0x4000");
    }
    if (root.load.segments.size() != plan_result.plan.segments.size()) {
        return fail("real ARM32 fixture graph load returned unexpected segment metadata");
    }
    if (!root.linker_strings.soname.has_value() ||
        *root.linker_strings.soname != kExpectedSoname) {
        return fail("real ARM32 fixture graph load did not preserve the pinned SONAME");
    }
    if (!root.linker_strings.needed.empty() ||
        !root.dependencies.empty() ||
        provider.calls != 0) {
        return fail("zero-dependency real ARM32 fixture unexpectedly resolved dependencies");
    }

    for (const auto& segment : root.load.segments) {
        if (!memory.is_mapped(segment.mapping_start) ||
            memory.permissions(segment.mapping_start) != segment.permissions) {
            return fail("real ARM32 fixture graph load did not retain final segment mappings");
        }
    }

    std::cout << "fixture.dependency_graph.load_bias=0x" << std::hex
              << root.load.load_bias << '\n'
              << "fixture.dependency_graph.required_alignment=0x"
              << plan_result.plan.required_load_bias_alignment << std::dec << '\n'
              << "fixture.dependency_graph.soname="
              << *root.linker_strings.soname << '\n'
              << "fixture.dependency_graph.needed_count="
              << root.linker_strings.needed.size() << '\n'
              << "fixture.dependency_graph.provider_calls=" << provider.calls << '\n'
              << "fixture.dependency_graph.status=PASS\n";
    return 0;
}
