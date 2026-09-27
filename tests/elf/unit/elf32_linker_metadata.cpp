#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "elf/elf32_linker_metadata.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DynamicEntry;
using liba32android::elf::Elf32LinkerMetadataError;
using liba32android::elf::build_elf32_linker_metadata;
using liba32android::elf::collect_elf32_linker_metadata;
using liba32android::elf::kElf32Df1Global;
using liba32android::memory::LinearGuestMemory;

constexpr std::int32_t kDtNull = 0;
constexpr std::int32_t kDtNeeded = 1;
constexpr std::int32_t kDtPltrelsz = 2;
constexpr std::int32_t kDtHash = 4;
constexpr std::int32_t kDtStrtab = 5;
constexpr std::int32_t kDtSymtab = 6;
constexpr std::int32_t kDtStrsz = 10;
constexpr std::int32_t kDtSyment = 11;
constexpr std::int32_t kDtInit = 12;
constexpr std::int32_t kDtFini = 13;
constexpr std::int32_t kDtSoname = 14;
constexpr std::int32_t kDtSymbolic = 16;
constexpr std::int32_t kDtRel = 17;
constexpr std::int32_t kDtRelsz = 18;
constexpr std::int32_t kDtRelent = 19;
constexpr std::int32_t kDtPltrel = 20;
constexpr std::int32_t kDtJmprel = 23;
constexpr std::int32_t kDtInitArray = 25;
constexpr std::int32_t kDtFiniArray = 26;
constexpr std::int32_t kDtInitArraySz = 27;
constexpr std::int32_t kDtFiniArraySz = 28;
constexpr std::int32_t kDtFlags = 30;
constexpr std::uint32_t kDfSymbolic = 0x2U;
constexpr std::int32_t kDtGnuHash = 0x6ffffef5;
constexpr std::int32_t kDtFlags1 = 0x6ffffffb;
constexpr std::int32_t kDtVersym = 0x6ffffff0;
constexpr std::int32_t kDtVerdef = 0x6ffffffc;
constexpr std::int32_t kDtVerdefnum = 0x6ffffffd;
constexpr std::int32_t kDtVerneed = 0x6ffffffe;
constexpr std::int32_t kDtVerneednum = 0x6fffffff;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

std::vector<Elf32DynamicEntry> full_entries() {
    return {
        {kDtStrtab, 0x100},
        {kDtStrsz, 0x80},
        {kDtSymtab, 0x200},
        {kDtSyment, 16},
        {kDtRel, 0x300},
        {kDtRelsz, 0x20},
        {kDtRelent, 8},
        {kDtJmprel, 0x380},
        {kDtPltrelsz, 0x10},
        {kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        {kDtSoname, 3},
        {kDtNeeded, 9},
        {kDtNeeded, 21},
        {kDtHash, 0x480},
        {kDtGnuHash, 0x400},
        {kDtFlags1, kElf32Df1Global | 0x1U},
        {kDtNull, 0},
    };
}

int test_valid_collection() {
    auto entries = full_entries();
    const auto result = collect_elf32_linker_metadata(entries);
    if (!result) return fail("valid linker metadata collection failed");

    if (!result.metadata.string_table.has_value() ||
        result.metadata.string_table->address_value != 0x100 ||
        result.metadata.string_table->size != 0x80) {
        return fail("string-table metadata was not collected exactly");
    }
    if (!result.metadata.symbol_table.has_value() ||
        result.metadata.symbol_table->address_value != 0x200 ||
        result.metadata.symbol_table->entry_size != 16) {
        return fail("symbol-table metadata was not collected exactly");
    }
    if (!result.metadata.rel_table.has_value() ||
        result.metadata.rel_table->address_value != 0x300 ||
        result.metadata.rel_table->size != 0x20 ||
        result.metadata.rel_table->entry_size != 8) {
        return fail("REL metadata was not collected exactly");
    }
    if (!result.metadata.plt_rel_table.has_value() ||
        result.metadata.plt_rel_table->address_value != 0x380 ||
        result.metadata.plt_rel_table->size != 0x10 ||
        result.metadata.plt_rel_table->entry_size != 8) {
        return fail("PLT REL metadata was not collected exactly");
    }
    if (!result.metadata.sysv_hash_table.has_value() ||
        result.metadata.sysv_hash_table->address_value != 0x480 ||
        !result.metadata.gnu_hash_table.has_value() ||
        result.metadata.gnu_hash_table->address_value != 0x400) {
        return fail("hash-table metadata was not collected exactly");
    }
    if (!result.metadata.soname_offset.has_value() ||
        *result.metadata.soname_offset != 3) {
        return fail("SONAME offset was not collected exactly");
    }
    if (result.metadata.needed_offsets != std::vector<std::uint32_t>{9, 21}) {
        return fail("DT_NEEDED offsets did not preserve dynamic-array order");
    }
    if (!result.metadata.flags_1.has_value() ||
        *result.metadata.flags_1 != (kElf32Df1Global | 0x1U) ||
        !result.metadata.global) {
        return fail("DT_FLAGS_1/DF_1_GLOBAL metadata was not collected exactly");
    }
    return 0;
}

int test_duplicate_singletons() {
    constexpr std::array<std::int32_t, 14> singleton_tags{
        kDtPltrelsz, kDtHash, kDtStrtab, kDtStrsz, kDtSymtab, kDtSyment,
        kDtRel, kDtRelsz, kDtRelent, kDtPltrel, kDtJmprel, kDtSoname,
        kDtGnuHash, kDtFlags1,
    };

    for (const std::int32_t tag : singleton_tags) {
        auto entries = full_entries();
        entries.insert(entries.end() - 1, Elf32DynamicEntry{tag, 0xabcdef01U});
        if (collect_elf32_linker_metadata(entries).error !=
            Elf32LinkerMetadataError::DuplicateSingleton) {
            return fail("recognized singleton duplicate was not rejected");
        }
    }
    return 0;
}

int test_incomplete_groups() {
    const std::array string_only{
        Elf32DynamicEntry{kDtStrtab, 0x100},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(string_only).error !=
        Elf32LinkerMetadataError::IncompleteStringTable) {
        return fail("incomplete STRTAB/STRSZ pair was not rejected");
    }

    const std::array needed_without_strings{
        Elf32DynamicEntry{kDtNeeded, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(needed_without_strings).error !=
        Elf32LinkerMetadataError::IncompleteStringTable) {
        return fail("DT_NEEDED without a string table was not rejected");
    }

    const std::array symbol_only{
        Elf32DynamicEntry{kDtSymtab, 0x200},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(symbol_only).error !=
        Elf32LinkerMetadataError::IncompleteSymbolTable) {
        return fail("incomplete SYMTAB/SYMENT pair was not rejected");
    }

    const std::array rel_partial{
        Elf32DynamicEntry{kDtRel, 0x300},
        Elf32DynamicEntry{kDtRelsz, 0x20},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(rel_partial).error !=
        Elf32LinkerMetadataError::IncompleteRelTable) {
        return fail("incomplete REL/RELSZ/RELENT group was not rejected");
    }

    for (unsigned mask = 1; mask < 7; ++mask) {
        std::vector<Elf32DynamicEntry> plt_partial;
        if ((mask & 1U) != 0) plt_partial.push_back({kDtJmprel, 0x380});
        if ((mask & 2U) != 0) plt_partial.push_back({kDtPltrelsz, 0x10});
        if ((mask & 4U) != 0) {
            plt_partial.push_back(
                {kDtPltrel, static_cast<std::uint32_t>(kDtRel)});
        }
        plt_partial.push_back({kDtNull, 0});
        if (collect_elf32_linker_metadata(plt_partial).error !=
            Elf32LinkerMetadataError::IncompletePltRelTable) {
            return fail("partial PLT REL group was not rejected");
        }
    }
    return 0;
}

int test_invalid_plt_rel_type() {
    const std::array entries{
        Elf32DynamicEntry{kDtJmprel, 0x380},
        Elf32DynamicEntry{kDtPltrelsz, 0x10},
        Elf32DynamicEntry{kDtPltrel, 7},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(entries).error !=
        Elf32LinkerMetadataError::InvalidPltRelType) {
        return fail("non-REL DT_PLTREL was not rejected");
    }
    return 0;
}

int test_unknown_tags_and_null_boundary() {
    const std::array entries{
        Elf32DynamicEntry{kDtGnuHash, 0x12345678U},
        Elf32DynamicEntry{0x70000001, 0x87654321U},
        Elf32DynamicEntry{kDtNull, 0},
        Elf32DynamicEntry{kDtStrtab, 0x100},
    };
    const auto result = collect_elf32_linker_metadata(entries);
    if (!result) return fail("recognized/unknown tags were rejected");
    if (!result.metadata.gnu_hash_table.has_value() ||
        result.metadata.gnu_hash_table->address_value != 0x12345678U ||
        result.metadata.string_table.has_value() ||
        result.metadata.symbol_table.has_value() ||
        result.metadata.sysv_hash_table.has_value() ||
        result.metadata.rel_table.has_value() ||
        result.metadata.plt_rel_table.has_value() ||
        result.metadata.soname_offset.has_value() ||
        !result.metadata.needed_offsets.empty()) {
        return fail("semantic collection did not honor hash/DT_NULL boundary");
    }
    return 0;
}

int test_symbolic_binding_metadata() {
    const std::array dt_symbolic{
        Elf32DynamicEntry{kDtSymbolic, 0},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto direct = collect_elf32_linker_metadata(dt_symbolic);
    if (!direct || !direct.metadata.symbolic) {
        return fail("DT_SYMBOLIC was not retained");
    }

    const std::array df_symbolic{
        Elf32DynamicEntry{kDtFlags, kDfSymbolic | 0x8U},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto flags = collect_elf32_linker_metadata(df_symbolic);
    if (!flags || !flags.metadata.symbolic) {
        return fail("DF_SYMBOLIC was not retained from DT_FLAGS");
    }

    const std::array both{
        Elf32DynamicEntry{kDtSymbolic, 0},
        Elf32DynamicEntry{kDtFlags, kDfSymbolic},
        Elf32DynamicEntry{kDtNull, 0},
    };
    LinearGuestMemory memory(0x100, 0);
    const auto built = build_elf32_linker_metadata(memory, 0, both);
    if (!built || !built.metadata.symbolic) {
        return fail("coexisting DT_SYMBOLIC/DF_SYMBOLIC was rejected or lost");
    }

    const std::array ordinary_flags{
        Elf32DynamicEntry{kDtFlags, 0x8U},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto ordinary = collect_elf32_linker_metadata(ordinary_flags);
    if (!ordinary || ordinary.metadata.symbolic) {
        return fail("non-symbolic DT_FLAGS incorrectly enabled requester-first binding");
    }

    const std::array duplicate_tag{
        Elf32DynamicEntry{kDtSymbolic, 0},
        Elf32DynamicEntry{kDtSymbolic, 0},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(duplicate_tag).error !=
        Elf32LinkerMetadataError::DuplicateSingleton) {
        return fail("duplicate DT_SYMBOLIC was not rejected");
    }

    const std::array duplicate_flags{
        Elf32DynamicEntry{kDtFlags, kDfSymbolic},
        Elf32DynamicEntry{kDtFlags, kDfSymbolic},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(duplicate_flags).error !=
        Elf32LinkerMetadataError::DuplicateSingleton) {
        return fail("duplicate DT_FLAGS was not rejected");
    }
    return 0;
}

int test_flags1_global_metadata() {
    const std::array non_global{
        Elf32DynamicEntry{kDtFlags1, 0x1U},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto collected = collect_elf32_linker_metadata(non_global);
    if (!collected || !collected.metadata.flags_1.has_value() ||
        *collected.metadata.flags_1 != 0x1U ||
        collected.metadata.global) {
        return fail("non-global DT_FLAGS_1 bits were not preserved exactly");
    }

    LinearGuestMemory memory(0x100, 0);
    const auto built = build_elf32_linker_metadata(memory, 0, non_global);
    if (!built || !built.metadata.flags_1.has_value() ||
        *built.metadata.flags_1 != 0x1U ||
        built.metadata.global) {
        return fail("validated non-global DT_FLAGS_1 metadata changed semantics");
    }
    return 0;
}

int test_lifecycle_array_metadata() {
    const std::array entries{
        Elf32DynamicEntry{kDtInit, 0x80},
        Elf32DynamicEntry{kDtFini, 0x90},
        Elf32DynamicEntry{kDtInitArray, 0x100},
        Elf32DynamicEntry{kDtInitArraySz, 8},
        Elf32DynamicEntry{kDtFiniArray, 0x200},
        Elf32DynamicEntry{kDtFiniArraySz, 12},
        Elf32DynamicEntry{kDtNull, 0},
    };

    const auto collected = collect_elf32_linker_metadata(entries);
    if (!collected ||
        !collected.metadata.init_function_address_value.has_value() ||
        *collected.metadata.init_function_address_value != 0x80U ||
        !collected.metadata.fini_function_address_value.has_value() ||
        *collected.metadata.fini_function_address_value != 0x90U ||
        !collected.metadata.init_array.has_value() ||
        collected.metadata.init_array->address_value != 0x100 ||
        collected.metadata.init_array->size != 8 ||
        !collected.metadata.fini_array.has_value() ||
        collected.metadata.fini_array->address_value != 0x200 ||
        collected.metadata.fini_array->size != 12) {
        return fail("lifecycle arrays were not collected exactly");
    }

    LinearGuestMemory memory(0x1000, 0x1000);
    const auto built = build_elf32_linker_metadata(memory, 0x1000, entries);
    if (!built ||
        !built.metadata.init_function.has_value() ||
        *built.metadata.init_function != 0x1080U ||
        !built.metadata.fini_function.has_value() ||
        *built.metadata.fini_function != 0x1090U ||
        !built.metadata.init_array.has_value() ||
        built.metadata.init_array->guest_address != 0x1100 ||
        built.metadata.init_array->size != 8 ||
        !built.metadata.fini_array.has_value() ||
        built.metadata.fini_array->guest_address != 0x1200 ||
        built.metadata.fini_array->size != 12) {
        return fail("lifecycle arrays were not rebased/validated exactly");
    }

    for (const std::int32_t tag :
         {kDtInit, kDtFini,
          kDtInitArray, kDtInitArraySz, kDtFiniArray, kDtFiniArraySz}) {
        std::vector<Elf32DynamicEntry> duplicate(entries.begin(), entries.end());
        duplicate.insert(duplicate.end() - 1,
                         Elf32DynamicEntry{tag, 0xabcdef00U});
        if (collect_elf32_linker_metadata(duplicate).error !=
            Elf32LinkerMetadataError::DuplicateSingleton) {
            return fail("lifecycle array singleton duplicate was not rejected");
        }
    }

    const std::array incomplete_init{
        Elf32DynamicEntry{kDtInitArray, 0x100},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(incomplete_init).error !=
        Elf32LinkerMetadataError::IncompleteInitArray) {
        return fail("incomplete INIT_ARRAY pair was not rejected");
    }

    const std::array incomplete_fini{
        Elf32DynamicEntry{kDtFiniArraySz, 8},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(incomplete_fini).error !=
        Elf32LinkerMetadataError::IncompleteFiniArray) {
        return fail("incomplete FINI_ARRAY pair was not rejected");
    }

    const std::array bad_size{
        Elf32DynamicEntry{kDtInitArray, 0x100},
        Elf32DynamicEntry{kDtInitArraySz, 6},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(bad_size).error !=
        Elf32LinkerMetadataError::InvalidFunctionArraySize) {
        return fail("non-integral lifecycle function array size was not rejected");
    }

    const std::array address_overflow{
        Elf32DynamicEntry{kDtInitArray, 0xfffffff0U},
        Elf32DynamicEntry{kDtInitArraySz, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x20, address_overflow).error !=
        Elf32LinkerMetadataError::AddressOverflow) {
        return fail("lifecycle array rebasing overflow was not rejected");
    }

    const std::array legacy_address_overflow{
        Elf32DynamicEntry{kDtInit, 0xfffffff0U},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(
            memory, 0x20U, legacy_address_overflow).error !=
        Elf32LinkerMetadataError::AddressOverflow) {
        return fail("legacy DT_INIT rebasing overflow was not rejected");
    }

    const std::array range_overflow{
        Elf32DynamicEntry{kDtFiniArray, 0xfffffffcU},
        Elf32DynamicEntry{kDtFiniArraySz, 8},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, range_overflow).error !=
        Elf32LinkerMetadataError::RangeOverflow) {
        return fail("lifecycle array range overflow was not rejected");
    }

    const std::array unreadable{
        Elf32DynamicEntry{kDtInitArray, 0x9000},
        Elf32DynamicEntry{kDtInitArraySz, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, unreadable).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("unreadable lifecycle array was not rejected");
    }

    const std::array zero_size{
        Elf32DynamicEntry{kDtFiniArray, 0x9000},
        Elf32DynamicEntry{kDtFiniArraySz, 0},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto empty = build_elf32_linker_metadata(memory, 0, zero_size);
    if (!empty || !empty.metadata.fini_array.has_value() ||
        empty.metadata.fini_array->guest_address != 0x9000 ||
        empty.metadata.fini_array->size != 0) {
        return fail("zero-length lifecycle array was not retained");
    }

    LinearGuestMemory guarded(0x100, 0x1000);
    const std::array<std::uint8_t, 4> sentinel{0xde, 0xad, 0xbe, 0xef};
    if (!guarded.write(0x1000, sentinel)) {
        return fail("could not stage lifecycle mutation guard");
    }
    if (build_elf32_linker_metadata(guarded, 0, unreadable).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("lifecycle mutation guard setup did not fail");
    }
    std::array<std::uint8_t, 4> after{};
    if (!guarded.read(0x1000, after) || after != sentinel) {
        return fail("failed lifecycle metadata validation mutated guest memory");
    }
    return 0;
}

int test_symbol_versioning_metadata() {
    const std::array entries{
        Elf32DynamicEntry{kDtStrtab, 0x100},
        Elf32DynamicEntry{kDtStrsz, 0x100},
        Elf32DynamicEntry{kDtSymtab, 0x300},
        Elf32DynamicEntry{kDtSyment, 16},
        Elf32DynamicEntry{kDtVersym, 0x400},
        Elf32DynamicEntry{kDtVerdef, 0x500},
        Elf32DynamicEntry{kDtVerdefnum, 2},
        Elf32DynamicEntry{kDtVerneed, 0x600},
        Elf32DynamicEntry{kDtVerneednum, 3},
        Elf32DynamicEntry{kDtNull, 0},
    };

    const auto collected = collect_elf32_linker_metadata(entries);
    if (!collected || !collected.metadata.has_symbol_versioning ||
        !collected.metadata.version_symbol_address_value.has_value() ||
        *collected.metadata.version_symbol_address_value != 0x400 ||
        !collected.metadata.version_definition_table.has_value() ||
        collected.metadata.version_definition_table->address_value != 0x500 ||
        collected.metadata.version_definition_table->count != 2 ||
        !collected.metadata.version_requirement_table.has_value() ||
        collected.metadata.version_requirement_table->address_value != 0x600 ||
        collected.metadata.version_requirement_table->count != 3) {
        return fail("symbol-version metadata was not collected exactly");
    }

    for (const std::int32_t tag :
         {kDtVersym, kDtVerdef, kDtVerdefnum, kDtVerneed, kDtVerneednum}) {
        std::vector<Elf32DynamicEntry> duplicate(entries.begin(), entries.end());
        duplicate.insert(duplicate.end() - 1,
                         Elf32DynamicEntry{tag, 0xabcdef01U});
        if (collect_elf32_linker_metadata(duplicate).error !=
            Elf32LinkerMetadataError::DuplicateSingleton) {
            return fail("symbol-version singleton duplicate was not rejected");
        }
    }

    LinearGuestMemory memory(0x2000, 0x1000);
    const auto built = build_elf32_linker_metadata(memory, 0x1000, entries);
    if (!built || !built.metadata.has_symbol_versioning ||
        !built.metadata.version_symbol_table.has_value() ||
        built.metadata.version_symbol_table->guest_address != 0x1400 ||
        !built.metadata.version_definition_table.has_value() ||
        built.metadata.version_definition_table->guest_address != 0x1500 ||
        built.metadata.version_definition_table->count != 2 ||
        !built.metadata.version_requirement_table.has_value() ||
        built.metadata.version_requirement_table->guest_address != 0x1600 ||
        built.metadata.version_requirement_table->count != 3) {
        return fail("symbol-version metadata was not rebased exactly once");
    }

    const std::array incomplete_def{
        Elf32DynamicEntry{kDtVerdef, 0x500},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(incomplete_def).error !=
        Elf32LinkerMetadataError::IncompleteVersionDefinitionTable) {
        return fail("incomplete VERDEF group was not rejected");
    }

    const std::array incomplete_need{
        Elf32DynamicEntry{kDtVerneednum, 1},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(incomplete_need).error !=
        Elf32LinkerMetadataError::IncompleteVersionRequirementTable) {
        return fail("incomplete VERNEED group was not rejected");
    }

    const std::array versym_without_symtab{
        Elf32DynamicEntry{kDtVersym, 0x400},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(versym_without_symtab).error !=
        Elf32LinkerMetadataError::IncompleteSymbolTable) {
        return fail("DT_VERSYM without DYNSYM metadata was not rejected");
    }

    const std::array verdef_without_strings{
        Elf32DynamicEntry{kDtVerdef, 0x500},
        Elf32DynamicEntry{kDtVerdefnum, 1},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (collect_elf32_linker_metadata(verdef_without_strings).error !=
        Elf32LinkerMetadataError::IncompleteStringTable) {
        return fail("VERDEF without STRTAB metadata was not rejected");
    }
    return 0;
}

int test_valid_rebasing_and_zero_bias() {
    auto entries = full_entries();

    LinearGuestMemory biased_memory(0x1000, 0x1000);
    const auto biased = build_elf32_linker_metadata(biased_memory, 0x1000, entries);
    if (!biased) return fail("valid rebased linker metadata failed");
    if (!biased.metadata.string_table.has_value() ||
        biased.metadata.string_table->guest_address != 0x1100 ||
        !biased.metadata.symbol_table.has_value() ||
        biased.metadata.symbol_table->guest_address != 0x1200 ||
        !biased.metadata.rel_table.has_value() ||
        biased.metadata.rel_table->guest_address != 0x1300 ||
        !biased.metadata.plt_rel_table.has_value() ||
        biased.metadata.plt_rel_table->guest_address != 0x1380 ||
        !biased.metadata.sysv_hash_table.has_value() ||
        biased.metadata.sysv_hash_table->guest_address != 0x1480 ||
        !biased.metadata.gnu_hash_table.has_value() ||
        biased.metadata.gnu_hash_table->guest_address != 0x1400) {
        return fail("pointer-like dynamic values were not rebased exactly once");
    }
    if (biased.metadata.needed_offsets != std::vector<std::uint32_t>{9, 21}) {
        return fail("validated metadata did not preserve DT_NEEDED order");
    }

    LinearGuestMemory fixed_memory(0x400, 0x100);
    const auto fixed = build_elf32_linker_metadata(fixed_memory, 0, entries);
    if (!fixed ||
        fixed.metadata.string_table->guest_address != 0x100 ||
        fixed.metadata.symbol_table->guest_address != 0x200 ||
        fixed.metadata.rel_table->guest_address != 0x300 ||
        fixed.metadata.plt_rel_table->guest_address != 0x380 ||
        fixed.metadata.sysv_hash_table->guest_address != 0x480 ||
        fixed.metadata.gnu_hash_table->guest_address != 0x400) {
        return fail("zero load bias did not preserve fixed guest addresses");
    }
    return 0;
}

int test_address_and_range_overflow() {
    LinearGuestMemory memory(0x100, 0);

    const std::array address_overflow{
        Elf32DynamicEntry{kDtStrtab, 0xfffffff0U},
        Elf32DynamicEntry{kDtStrsz, 1},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x20, address_overflow).error !=
        Elf32LinkerMetadataError::AddressOverflow) {
        return fail("rebased pointer overflow was not rejected");
    }

    const std::array range_overflow{
        Elf32DynamicEntry{kDtStrtab, 0xfffffff0U},
        Elf32DynamicEntry{kDtStrsz, 0x20},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, range_overflow).error !=
        Elf32LinkerMetadataError::RangeOverflow) {
        return fail("guest range overflow was not rejected");
    }

    const std::array hash_address_overflow{
        Elf32DynamicEntry{kDtHash, 0xfffffff0U},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x20, hash_address_overflow).error !=
        Elf32LinkerMetadataError::AddressOverflow) {
        return fail("hash pointer rebasing overflow was not rejected");
    }

    const std::array plt_address_overflow{
        Elf32DynamicEntry{kDtJmprel, 0xfffffff0U},
        Elf32DynamicEntry{kDtPltrelsz, 8},
        Elf32DynamicEntry{kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x20, plt_address_overflow).error !=
        Elf32LinkerMetadataError::AddressOverflow) {
        return fail("PLT REL pointer rebasing overflow was not rejected");
    }

    const std::array plt_range_overflow{
        Elf32DynamicEntry{kDtJmprel, 0xfffffff8U},
        Elf32DynamicEntry{kDtPltrelsz, 16},
        Elf32DynamicEntry{kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, plt_range_overflow).error !=
        Elf32LinkerMetadataError::RangeOverflow) {
        return fail("PLT REL range overflow was not rejected");
    }
    return 0;
}

int test_unreadable_ranges() {
    LinearGuestMemory memory(0x100, 0x1000);

    const std::array bad_strings{
        Elf32DynamicEntry{kDtStrtab, 0x9000},
        Elf32DynamicEntry{kDtStrsz, 1},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, bad_strings).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("unreadable string table was not rejected");
    }

    const std::array bad_symbols{
        Elf32DynamicEntry{kDtSymtab, 0x9000},
        Elf32DynamicEntry{kDtSyment, 16},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, bad_symbols).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("unreadable symbol table was not rejected");
    }

    const std::array bad_rel{
        Elf32DynamicEntry{kDtRel, 0x9000},
        Elf32DynamicEntry{kDtRelsz, 8},
        Elf32DynamicEntry{kDtRelent, 8},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, bad_rel).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("unreadable REL table was not rejected");
    }

    const std::array bad_plt_rel{
        Elf32DynamicEntry{kDtJmprel, 0x9000},
        Elf32DynamicEntry{kDtPltrelsz, 8},
        Elf32DynamicEntry{kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0, bad_plt_rel).error !=
        Elf32LinkerMetadataError::ReadFailed) {
        return fail("unreadable PLT REL table was not rejected");
    }

    for (const std::int32_t hash_tag : {kDtHash, kDtGnuHash}) {
        const std::array bad_hash{
            Elf32DynamicEntry{hash_tag, 0x9000},
            Elf32DynamicEntry{kDtNull, 0},
        };
        if (build_elf32_linker_metadata(memory, 0, bad_hash).error !=
            Elf32LinkerMetadataError::ReadFailed) {
            return fail("unreadable hash header was not rejected");
        }
    }
    return 0;
}

int test_entry_sizes_and_rel_size() {
    LinearGuestMemory memory(0x1000, 0x1000);

    const std::array bad_syment{
        Elf32DynamicEntry{kDtSymtab, 0x100},
        Elf32DynamicEntry{kDtSyment, 15},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_syment).error !=
        Elf32LinkerMetadataError::InvalidSymbolEntrySize) {
        return fail("invalid DT_SYMENT was not rejected");
    }

    const std::array bad_relent{
        Elf32DynamicEntry{kDtRel, 0x300},
        Elf32DynamicEntry{kDtRelsz, 8},
        Elf32DynamicEntry{kDtRelent, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_relent).error !=
        Elf32LinkerMetadataError::InvalidRelEntrySize) {
        return fail("invalid DT_RELENT was not rejected");
    }

    const std::array bad_relsz{
        Elf32DynamicEntry{kDtRel, 0x300},
        Elf32DynamicEntry{kDtRelsz, 10},
        Elf32DynamicEntry{kDtRelent, 8},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_relsz).error !=
        Elf32LinkerMetadataError::InvalidRelSize) {
        return fail("non-integral REL byte size was not rejected");
    }

    const std::array bad_pltrelsz{
        Elf32DynamicEntry{kDtJmprel, 0x380},
        Elf32DynamicEntry{kDtPltrelsz, 10},
        Elf32DynamicEntry{kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_pltrelsz).error !=
        Elf32LinkerMetadataError::InvalidPltRelSize) {
        return fail("non-integral PLT REL byte size was not rejected");
    }
    return 0;
}

int test_zero_length_plt_rel() {
    LinearGuestMemory memory(0x100, 0x1000);
    const std::array entries{
        Elf32DynamicEntry{kDtJmprel, 0x9000},
        Elf32DynamicEntry{kDtPltrelsz, 0},
        Elf32DynamicEntry{kDtPltrel, static_cast<std::uint32_t>(kDtRel)},
        Elf32DynamicEntry{kDtNull, 0},
    };
    const auto result = build_elf32_linker_metadata(memory, 0, entries);
    if (!result || !result.metadata.plt_rel_table.has_value() ||
        result.metadata.plt_rel_table->guest_address != 0x9000 ||
        result.metadata.plt_rel_table->size != 0 ||
        result.metadata.plt_rel_table->entry_size != 8) {
        return fail("zero-length PLT REL table was not accepted");
    }
    return 0;
}

int test_string_offsets() {
    LinearGuestMemory memory(0x1000, 0x1000);

    const std::array bad_soname{
        Elf32DynamicEntry{kDtStrtab, 0x100},
        Elf32DynamicEntry{kDtStrsz, 4},
        Elf32DynamicEntry{kDtSoname, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_soname).error !=
        Elf32LinkerMetadataError::StringOffsetOutOfRange) {
        return fail("out-of-range DT_SONAME offset was not rejected");
    }

    const std::array bad_needed{
        Elf32DynamicEntry{kDtStrtab, 0x100},
        Elf32DynamicEntry{kDtStrsz, 4},
        Elf32DynamicEntry{kDtNeeded, 4},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_needed).error !=
        Elf32LinkerMetadataError::StringOffsetOutOfRange) {
        return fail("out-of-range DT_NEEDED offset was not rejected");
    }
    return 0;
}

int test_failure_does_not_mutate_guest_memory() {
    LinearGuestMemory memory(0x100, 0x1000);
    const std::array<std::uint8_t, 4> sentinel{0xde, 0xad, 0xbe, 0xef};
    if (!memory.write(0x1000, sentinel)) return fail("could not stage sentinel bytes");

    const std::array bad_syment{
        Elf32DynamicEntry{kDtSymtab, 0x10},
        Elf32DynamicEntry{kDtSyment, 15},
        Elf32DynamicEntry{kDtNull, 0},
    };
    if (build_elf32_linker_metadata(memory, 0x1000, bad_syment).error !=
        Elf32LinkerMetadataError::InvalidSymbolEntrySize) {
        return fail("mutation guard setup did not fail as expected");
    }

    std::array<std::uint8_t, 4> after{};
    if (!memory.read(0x1000, after) || after != sentinel) {
        return fail("failed metadata validation mutated guest memory");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_valid_collection(); status != 0) return status;
    if (const int status = test_duplicate_singletons(); status != 0) return status;
    if (const int status = test_incomplete_groups(); status != 0) return status;
    if (const int status = test_invalid_plt_rel_type(); status != 0) return status;
    if (const int status = test_unknown_tags_and_null_boundary(); status != 0) return status;
    if (const int status = test_symbolic_binding_metadata(); status != 0) return status;
    if (const int status = test_flags1_global_metadata(); status != 0) return status;
    if (const int status = test_lifecycle_array_metadata(); status != 0) return status;
    if (const int status = test_symbol_versioning_metadata(); status != 0) return status;
    if (const int status = test_valid_rebasing_and_zero_bias(); status != 0) return status;
    if (const int status = test_address_and_range_overflow(); status != 0) return status;
    if (const int status = test_unreadable_ranges(); status != 0) return status;
    if (const int status = test_entry_sizes_and_rel_size(); status != 0) return status;
    if (const int status = test_zero_length_plt_rel(); status != 0) return status;
    if (const int status = test_string_offsets(); status != 0) return status;
    if (const int status = test_failure_does_not_mutate_guest_memory(); status != 0) return status;
    return 0;
}
