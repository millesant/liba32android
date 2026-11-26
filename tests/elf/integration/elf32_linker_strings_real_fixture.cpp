#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dynamic.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_linker_strings.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32LinkerStringOptions;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::build_elf32_linker_metadata;
using liba32android::elf::build_elf32_linker_strings;
using liba32android::elf::load_elf32;
using liba32android::elf::parse_elf32_dynamic;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint32_t kFixtureDynamicBase = 0x02000000U;
constexpr std::uint32_t kMaxFixtureNameBytes = 64;
constexpr const char* kExpectedSoname = "liba32android_loader_fixture.so";

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
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
    Elf32LoadOptions options;
    options.dynamic_base = kFixtureDynamicBase;

    const auto load_result = load_elf32(memory, image, options);
    if (!load_result) {
        return fail(std::string("real ARM32 fixture load failed before linker strings: ") +
                    liba32android::elf::to_string(load_result.error));
    }
    if (!load_result.dynamic_segment.has_value()) {
        return fail("real ARM32 fixture did not expose PT_DYNAMIC metadata");
    }

    const auto dynamic_result = parse_elf32_dynamic(memory, *load_result.dynamic_segment);
    if (!dynamic_result) {
        return fail(std::string("real ARM32 fixture dynamic parse failed before linker strings: ") +
                    liba32android::elf::to_string(dynamic_result.error));
    }

    const auto metadata_result =
        build_elf32_linker_metadata(memory, load_result.load_bias, dynamic_result.entries);
    if (!metadata_result) {
        return fail(std::string("real ARM32 fixture linker metadata failed before strings: ") +
                    liba32android::elf::to_string(metadata_result.error));
    }

    const auto strings_result = build_elf32_linker_strings(
        memory,
        metadata_result.metadata,
        Elf32LinkerStringOptions{.max_string_bytes = kMaxFixtureNameBytes});
    if (!strings_result) {
        return fail(std::string("real ARM32 fixture linker strings failed: ") +
                    liba32android::elf::to_string(strings_result.error));
    }

    if (!strings_result.strings.soname.has_value() ||
        *strings_result.strings.soname != kExpectedSoname) {
        return fail("real ARM32 fixture SONAME did not match the pinned fixture build");
    }
    if (!strings_result.strings.needed.empty()) {
        return fail("freestanding real ARM32 fixture unexpectedly materialized NEEDED names");
    }

    std::cout << "fixture.linker_strings.soname=" << *strings_result.strings.soname << '\n'
              << "fixture.linker_strings.needed_count="
              << strings_result.strings.needed.size() << '\n'
              << "fixture.linker_strings.max_name_bytes="
              << kMaxFixtureNameBytes << '\n'
              << "fixture.linker_strings.status=PASS\n";
    return 0;
}
