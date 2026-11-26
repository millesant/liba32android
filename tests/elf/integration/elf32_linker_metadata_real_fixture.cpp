#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dynamic.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_loader.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DynamicEntry;
using liba32android::elf::Elf32LoadOptions;
using liba32android::elf::build_elf32_linker_metadata;
using liba32android::elf::load_elf32;
using liba32android::elf::parse_elf32_dynamic;
using liba32android::memory::MappedGuestMemory;

constexpr std::uint32_t kFixtureDynamicBase = 0x02000000U;
constexpr std::int32_t kDtNeeded = 1;
constexpr std::int32_t kDtPltrelsz = 2;
constexpr std::int32_t kDtStrtab = 5;
constexpr std::int32_t kDtSymtab = 6;
constexpr std::int32_t kDtStrsz = 10;
constexpr std::int32_t kDtSyment = 11;
constexpr std::int32_t kDtSoname = 14;
constexpr std::int32_t kDtRel = 17;
constexpr std::int32_t kDtRelsz = 18;
constexpr std::int32_t kDtRelent = 19;
constexpr std::int32_t kDtPltrel = 20;
constexpr std::int32_t kDtJmprel = 23;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

std::optional<std::uint32_t> find_tag(const std::vector<Elf32DynamicEntry>& entries,
                                      std::int32_t tag) {
    for (const Elf32DynamicEntry& entry : entries) {
        if (entry.tag == tag) return entry.value;
        if (entry.tag == 0) break;
    }
    return std::nullopt;
}

bool has_tag(const std::vector<Elf32DynamicEntry>& entries, std::int32_t tag) {
    return find_tag(entries, tag).has_value();
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
        return fail(std::string("real ARM32 fixture load failed before linker metadata: ") +
                    liba32android::elf::to_string(load_result.error));
    }
    if (!load_result.dynamic_segment.has_value()) {
        return fail("real ARM32 fixture did not expose PT_DYNAMIC metadata");
    }

    const auto dynamic_result = parse_elf32_dynamic(memory, *load_result.dynamic_segment);
    if (!dynamic_result) {
        return fail(std::string("real ARM32 fixture dynamic parse failed: ") +
                    liba32android::elf::to_string(dynamic_result.error));
    }

    const auto metadata_result =
        build_elf32_linker_metadata(memory, load_result.load_bias, dynamic_result.entries);
    if (!metadata_result) {
        return fail(std::string("real ARM32 fixture linker metadata failed: ") +
                    liba32android::elf::to_string(metadata_result.error));
    }

    const auto raw_strtab = find_tag(dynamic_result.entries, kDtStrtab);
    const auto raw_strsz = find_tag(dynamic_result.entries, kDtStrsz);
    const auto raw_symtab = find_tag(dynamic_result.entries, kDtSymtab);
    const auto raw_syment = find_tag(dynamic_result.entries, kDtSyment);
    const auto raw_rel = find_tag(dynamic_result.entries, kDtRel);
    const auto raw_relsz = find_tag(dynamic_result.entries, kDtRelsz);
    const auto raw_relent = find_tag(dynamic_result.entries, kDtRelent);
    const auto raw_soname = find_tag(dynamic_result.entries, kDtSoname);

    if (!raw_strtab || !raw_strsz || !raw_symtab || !raw_syment ||
        !raw_rel || !raw_relsz || !raw_relent || !raw_soname) {
        return fail("real fixture is missing expected supported linker metadata tags");
    }

    if (!metadata_result.metadata.string_table.has_value() ||
        metadata_result.metadata.string_table->guest_address !=
            *raw_strtab + load_result.load_bias ||
        metadata_result.metadata.string_table->size != *raw_strsz) {
        return fail("real fixture STRTAB metadata did not match raw tags plus load bias");
    }

    if (!metadata_result.metadata.symbol_table.has_value() ||
        metadata_result.metadata.symbol_table->guest_address !=
            *raw_symtab + load_result.load_bias ||
        metadata_result.metadata.symbol_table->entry_size != *raw_syment) {
        return fail("real fixture SYMTAB metadata did not match raw tags plus load bias");
    }

    if (!metadata_result.metadata.rel_table.has_value() ||
        metadata_result.metadata.rel_table->guest_address !=
            *raw_rel + load_result.load_bias ||
        metadata_result.metadata.rel_table->size != *raw_relsz ||
        metadata_result.metadata.rel_table->entry_size != *raw_relent) {
        return fail("real fixture REL metadata did not match raw tags plus load bias");
    }

    if (!metadata_result.metadata.soname_offset.has_value() ||
        *metadata_result.metadata.soname_offset != *raw_soname) {
        return fail("real fixture SONAME offset did not match the raw dynamic tag");
    }

    if (has_tag(dynamic_result.entries, kDtNeeded) ||
        !metadata_result.metadata.needed_offsets.empty()) {
        return fail("freestanding real fixture unexpectedly gained DT_NEEDED metadata");
    }

    if (has_tag(dynamic_result.entries, kDtJmprel) ||
        has_tag(dynamic_result.entries, kDtPltrelsz) ||
        has_tag(dynamic_result.entries, kDtPltrel) ||
        metadata_result.metadata.plt_rel_table.has_value()) {
        return fail("freestanding real fixture unexpectedly gained PLT REL metadata");
    }

    std::cout << "fixture.linker_metadata.strtab_guest=0x" << std::hex
              << metadata_result.metadata.string_table->guest_address << '\n'
              << "fixture.linker_metadata.symtab_guest=0x"
              << metadata_result.metadata.symbol_table->guest_address << '\n'
              << "fixture.linker_metadata.rel_guest=0x"
              << metadata_result.metadata.rel_table->guest_address << '\n'
              << std::dec
              << "fixture.linker_metadata.rel_size="
              << metadata_result.metadata.rel_table->size << '\n'
              << "fixture.linker_metadata.has_soname=true\n"
              << "fixture.linker_metadata.has_needed=false\n"
              << "fixture.linker_metadata.has_plt_rel=false\n"
              << "fixture.linker_metadata.status=PASS\n";
    return 0;
}
