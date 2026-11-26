#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dynamic.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DynamicEntry;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::load_elf32;
using liba32android::elf::parse_elf32_dynamic;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint32_t kFixtureDynamicBase = 0x02000000U;
constexpr std::int32_t kDtNeeded = 1;
constexpr std::int32_t kDtStrtab = 5;
constexpr std::int32_t kDtSymtab = 6;
constexpr std::int32_t kDtStrsz = 10;
constexpr std::int32_t kDtSyment = 11;
constexpr std::int32_t kDtSoname = 14;
constexpr std::int32_t kDtRel = 17;
constexpr std::int32_t kDtRelsz = 18;
constexpr std::int32_t kDtRelent = 19;
constexpr std::int32_t kDtGnuHash = 0x6ffffef5;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

bool has_tag(const std::vector<Elf32DynamicEntry>& entries, std::int32_t tag) {
    for (const Elf32DynamicEntry& entry : entries) {
        if (entry.tag == tag) return true;
    }
    return false;
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
        return fail(std::string("real ARM32 fixture load failed before dynamic parsing: ") +
                    liba32android::elf::to_string(load_result.error));
    }
    if (!load_result.dynamic_segment.has_value()) {
        return fail("real ARM32 fixture did not expose PT_DYNAMIC metadata");
    }

    const auto dynamic_result = parse_elf32_dynamic(memory, *load_result.dynamic_segment);
    if (!dynamic_result) {
        return fail(std::string("real ARM32 fixture dynamic array parse failed: ") +
                    liba32android::elf::to_string(dynamic_result.error));
    }
    if (dynamic_result.entries.empty() || dynamic_result.entries.back().tag != 0) {
        return fail("real ARM32 fixture dynamic array did not end with preserved DT_NULL");
    }

    if (!has_tag(dynamic_result.entries, kDtStrtab) ||
        !has_tag(dynamic_result.entries, kDtSymtab) ||
        !has_tag(dynamic_result.entries, kDtStrsz) ||
        !has_tag(dynamic_result.entries, kDtSyment) ||
        !has_tag(dynamic_result.entries, kDtSoname) ||
        !has_tag(dynamic_result.entries, kDtRel) ||
        !has_tag(dynamic_result.entries, kDtRelsz) ||
        !has_tag(dynamic_result.entries, kDtRelent) ||
        !has_tag(dynamic_result.entries, kDtGnuHash)) {
        return fail("real ARM32 fixture dynamic array is missing expected structural tags");
    }
    if (has_tag(dynamic_result.entries, kDtNeeded)) {
        return fail("freestanding real ARM32 fixture unexpectedly gained DT_NEEDED");
    }

    std::cout << "fixture.dynamic.entry_count=" << dynamic_result.entries.size() << '\n'
              << "fixture.dynamic.has_strtab=true\n"
              << "fixture.dynamic.has_symtab=true\n"
              << "fixture.dynamic.has_rel=true\n"
              << "fixture.dynamic.has_gnu_hash=true\n"
              << "fixture.dynamic.has_needed=false\n"
              << "fixture.dynamic.status=PASS\n";
    return 0;
}
