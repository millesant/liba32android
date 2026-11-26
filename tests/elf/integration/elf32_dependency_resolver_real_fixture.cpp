#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_resolver.h"
#include "elf/elf32_dynamic.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_linker_strings.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32DependencyResolveOptions;
using liba32android::elf::Elf32LinkerStringOptions;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::build_elf32_linker_metadata;
using liba32android::elf::build_elf32_linker_strings;
using liba32android::elf::load_elf32;
using liba32android::elf::parse_elf32_dynamic;
using liba32android::elf::resolve_elf32_dependencies;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint32_t kFixtureDynamicBase = 0x02000000U;
constexpr std::uint32_t kMaxFixtureNameBytes = 64;
constexpr std::uint32_t kMaxDependencies = 8;
constexpr std::uint64_t kMaxDependencyImageBytes = 1U << 20;
constexpr std::uint64_t kMaxTotalDependencyImageBytes = 4U << 20;

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
    Elf32LoadOptions load_options;
    load_options.dynamic_base = kFixtureDynamicBase;

    const auto load_result = load_elf32(memory, image, load_options);
    if (!load_result) {
        return fail(std::string("real ARM32 fixture load failed before dependency resolution: ") +
                    liba32android::elf::to_string(load_result.error));
    }
    if (!load_result.dynamic_segment.has_value()) {
        return fail("real ARM32 fixture did not expose PT_DYNAMIC metadata");
    }

    const auto dynamic_result = parse_elf32_dynamic(memory, *load_result.dynamic_segment);
    if (!dynamic_result) {
        return fail(std::string("real ARM32 fixture dynamic parse failed before dependency resolution: ") +
                    liba32android::elf::to_string(dynamic_result.error));
    }

    const auto metadata_result =
        build_elf32_linker_metadata(memory, load_result.load_bias, dynamic_result.entries);
    if (!metadata_result) {
        return fail(std::string("real ARM32 fixture linker metadata failed before dependency resolution: ") +
                    liba32android::elf::to_string(metadata_result.error));
    }

    const auto strings_result = build_elf32_linker_strings(
        memory,
        metadata_result.metadata,
        Elf32LinkerStringOptions{.max_string_bytes = kMaxFixtureNameBytes});
    if (!strings_result) {
        return fail(std::string("real ARM32 fixture linker strings failed before dependency resolution: ") +
                    liba32android::elf::to_string(strings_result.error));
    }
    if (!strings_result.strings.needed.empty()) {
        return fail("freestanding real ARM32 fixture unexpectedly materialized NEEDED names");
    }

    FailIfCalledProvider provider;
    const auto dependencies = resolve_elf32_dependencies(
        strings_result.strings,
        provider,
        Elf32DependencyResolveOptions{
            .max_dependencies = kMaxDependencies,
            .max_image_bytes = kMaxDependencyImageBytes,
            .max_total_image_bytes = kMaxTotalDependencyImageBytes,
        });
    if (!dependencies) {
        return fail(std::string("zero-dependency resolution failed: ") +
                    liba32android::elf::to_string(dependencies.error));
    }
    if (!dependencies.dependencies.ordered.empty()) {
        return fail("zero-dependency fixture produced resolved dependency entries");
    }
    if (provider.calls != 0) {
        return fail("dependency provider was invoked for a zero-dependency fixture");
    }

    std::cout << "fixture.dependency_resolution.needed_count="
              << strings_result.strings.needed.size() << '\n'
              << "fixture.dependency_resolution.resolved_count="
              << dependencies.dependencies.ordered.size() << '\n'
              << "fixture.dependency_resolution.provider_calls=" << provider.calls << '\n'
              << "fixture.dependency_resolution.status=PASS\n";
    return 0;
}
