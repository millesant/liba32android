#include "elf/elf32_linker_metadata.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace liba32android::elf {
namespace {

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

constexpr std::uint32_t kElf32SymbolEntrySize = 16;
constexpr std::uint32_t kElf32RelEntrySize = 8;
constexpr std::uint32_t kSysvHashHeaderSize = 8;
constexpr std::uint32_t kGnuHashHeaderSize = 16;
constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;
constexpr std::size_t kReadValidationChunkSize = 256;

[[nodiscard]] Elf32CollectedLinkerMetadataResult collection_failure(
    Elf32LinkerMetadataError error) {
    Elf32CollectedLinkerMetadataResult result;
    result.error = error;
    return result;
}

[[nodiscard]] Elf32LinkerMetadataResult validation_failure(
    Elf32LinkerMetadataError error) {
    Elf32LinkerMetadataResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool assign_singleton(std::optional<std::uint32_t>& field,
                                    std::uint32_t value) {
    if (field.has_value()) return false;
    field = value;
    return true;
}

[[nodiscard]] bool pair_complete(const std::optional<std::uint32_t>& first,
                                 const std::optional<std::uint32_t>& second) {
    return first.has_value() == second.has_value();
}

[[nodiscard]] bool rebase_address(std::uint32_t value,
                                  std::uint32_t load_bias,
                                  std::uint32_t& guest_address) {
    const std::uint64_t rebased =
        static_cast<std::uint64_t>(value) + static_cast<std::uint64_t>(load_bias);
    if (rebased > std::numeric_limits<std::uint32_t>::max()) return false;
    guest_address = static_cast<std::uint32_t>(rebased);
    return true;
}

[[nodiscard]] bool range_fits(std::uint32_t guest_address, std::uint32_t size) {
    return static_cast<std::uint64_t>(guest_address) + size <= kGuestAddressSpaceSize;
}

[[nodiscard]] bool readable_range(const memory::GuestMemory& memory,
                                  std::uint32_t guest_address,
                                  std::uint32_t size) {
    if (size == 0) return true;

    std::array<std::uint8_t, kReadValidationChunkSize> buffer{};
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), static_cast<std::uint64_t>(size) - offset));
        const std::uint64_t current = static_cast<std::uint64_t>(guest_address) + offset;
        if (current > std::numeric_limits<std::uint32_t>::max() ||
            !memory.read(static_cast<std::uint32_t>(current),
                         std::span<std::uint8_t>(buffer.data(), chunk))) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

}  // namespace

Elf32CollectedLinkerMetadataResult collect_elf32_linker_metadata(
    std::span<const Elf32DynamicEntry> entries) {
    std::optional<std::uint32_t> strtab;
    std::optional<std::uint32_t> strsz;
    std::optional<std::uint32_t> symtab;
    std::optional<std::uint32_t> syment;
    std::optional<std::uint32_t> sysv_hash;
    std::optional<std::uint32_t> gnu_hash;
    std::optional<std::uint32_t> rel;
    std::optional<std::uint32_t> relsz;
    std::optional<std::uint32_t> relent;
    std::optional<std::uint32_t> jmprel;
    std::optional<std::uint32_t> pltrelsz;
    std::optional<std::uint32_t> pltrel;
    std::optional<std::uint32_t> init;
    std::optional<std::uint32_t> fini;
    std::optional<std::uint32_t> init_array;
    std::optional<std::uint32_t> init_array_size;
    std::optional<std::uint32_t> fini_array;
    std::optional<std::uint32_t> fini_array_size;
    std::optional<std::uint32_t> versym;
    std::optional<std::uint32_t> verdef;
    std::optional<std::uint32_t> verdefnum;
    std::optional<std::uint32_t> verneed;
    std::optional<std::uint32_t> verneednum;
    std::optional<std::uint32_t> soname;
    std::optional<std::uint32_t> flags;
    std::optional<std::uint32_t> flags_1;
    bool has_dt_symbolic = false;

    Elf32CollectedLinkerMetadataResult result;

    for (const Elf32DynamicEntry& entry : entries) {
        if (entry.tag == kDtNull) break;

        bool accepted = true;
        switch (entry.tag) {
        case kDtNeeded:
            result.metadata.needed_offsets.push_back(entry.value);
            break;
        case kDtPltrelsz:
            accepted = assign_singleton(pltrelsz, entry.value);
            break;
        case kDtHash:
            accepted = assign_singleton(sysv_hash, entry.value);
            break;
        case kDtStrtab:
            accepted = assign_singleton(strtab, entry.value);
            break;
        case kDtSymtab:
            accepted = assign_singleton(symtab, entry.value);
            break;
        case kDtStrsz:
            accepted = assign_singleton(strsz, entry.value);
            break;
        case kDtSyment:
            accepted = assign_singleton(syment, entry.value);
            break;
        case kDtInit:
            accepted = assign_singleton(init, entry.value);
            break;
        case kDtFini:
            accepted = assign_singleton(fini, entry.value);
            break;
        case kDtSoname:
            accepted = assign_singleton(soname, entry.value);
            break;
        case kDtSymbolic:
            if (has_dt_symbolic) {
                accepted = false;
            } else {
                has_dt_symbolic = true;
            }
            break;
        case kDtRel:
            accepted = assign_singleton(rel, entry.value);
            break;
        case kDtRelsz:
            accepted = assign_singleton(relsz, entry.value);
            break;
        case kDtRelent:
            accepted = assign_singleton(relent, entry.value);
            break;
        case kDtPltrel:
            accepted = assign_singleton(pltrel, entry.value);
            break;
        case kDtJmprel:
            accepted = assign_singleton(jmprel, entry.value);
            break;
        case kDtInitArray:
            accepted = assign_singleton(init_array, entry.value);
            break;
        case kDtFiniArray:
            accepted = assign_singleton(fini_array, entry.value);
            break;
        case kDtInitArraySz:
            accepted = assign_singleton(init_array_size, entry.value);
            break;
        case kDtFiniArraySz:
            accepted = assign_singleton(fini_array_size, entry.value);
            break;
        case kDtFlags:
            accepted = assign_singleton(flags, entry.value);
            break;
        case kDtGnuHash:
            accepted = assign_singleton(gnu_hash, entry.value);
            break;
        case kDtFlags1:
            accepted = assign_singleton(flags_1, entry.value);
            break;
        case kDtVersym:
            accepted = assign_singleton(versym, entry.value);
            break;
        case kDtVerdef:
            accepted = assign_singleton(verdef, entry.value);
            break;
        case kDtVerdefnum:
            accepted = assign_singleton(verdefnum, entry.value);
            break;
        case kDtVerneed:
            accepted = assign_singleton(verneed, entry.value);
            break;
        case kDtVerneednum:
            accepted = assign_singleton(verneednum, entry.value);
            break;
        default:
            break;
        }

        if (!accepted) {
            return collection_failure(Elf32LinkerMetadataError::DuplicateSingleton);
        }
    }

    if (!pair_complete(strtab, strsz) ||
        ((soname.has_value() || !result.metadata.needed_offsets.empty()) &&
         !strtab.has_value())) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteStringTable);
    }
    if (!pair_complete(symtab, syment)) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteSymbolTable);
    }

    const unsigned rel_fields =
        static_cast<unsigned>(rel.has_value()) +
        static_cast<unsigned>(relsz.has_value()) +
        static_cast<unsigned>(relent.has_value());
    if (rel_fields != 0 && rel_fields != 3) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteRelTable);
    }

    const unsigned plt_rel_fields =
        static_cast<unsigned>(jmprel.has_value()) +
        static_cast<unsigned>(pltrelsz.has_value()) +
        static_cast<unsigned>(pltrel.has_value());
    if (plt_rel_fields != 0 && plt_rel_fields != 3) {
        return collection_failure(Elf32LinkerMetadataError::IncompletePltRelTable);
    }
    if (pltrel.has_value() && *pltrel != static_cast<std::uint32_t>(kDtRel)) {
        return collection_failure(Elf32LinkerMetadataError::InvalidPltRelType);
    }

    if (!pair_complete(init_array, init_array_size)) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteInitArray);
    }
    if (!pair_complete(fini_array, fini_array_size)) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteFiniArray);
    }
    if ((init_array_size.has_value() && (*init_array_size % 4U) != 0) ||
        (fini_array_size.has_value() && (*fini_array_size % 4U) != 0)) {
        return collection_failure(Elf32LinkerMetadataError::InvalidFunctionArraySize);
    }

    if (!pair_complete(verdef, verdefnum)) {
        return collection_failure(
            Elf32LinkerMetadataError::IncompleteVersionDefinitionTable);
    }
    if (!pair_complete(verneed, verneednum)) {
        return collection_failure(
            Elf32LinkerMetadataError::IncompleteVersionRequirementTable);
    }
    if (versym.has_value() && !symtab.has_value()) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteSymbolTable);
    }
    if ((verdef.has_value() || verneed.has_value()) && !strtab.has_value()) {
        return collection_failure(Elf32LinkerMetadataError::IncompleteStringTable);
    }

    if (strtab.has_value()) {
        result.metadata.string_table = Elf32CollectedStringTableMetadata{
            .address_value = *strtab,
            .size = *strsz,
        };
    }
    if (symtab.has_value()) {
        result.metadata.symbol_table = Elf32CollectedSymbolTableMetadata{
            .address_value = *symtab,
            .entry_size = *syment,
        };
    }
    if (sysv_hash.has_value()) {
        result.metadata.sysv_hash_table = Elf32CollectedHashTableMetadata{
            .address_value = *sysv_hash,
        };
    }
    if (gnu_hash.has_value()) {
        result.metadata.gnu_hash_table = Elf32CollectedHashTableMetadata{
            .address_value = *gnu_hash,
        };
    }
    if (rel.has_value()) {
        result.metadata.rel_table = Elf32CollectedRelTableMetadata{
            .address_value = *rel,
            .size = *relsz,
            .entry_size = *relent,
        };
    }
    if (jmprel.has_value()) {
        result.metadata.plt_rel_table = Elf32CollectedRelTableMetadata{
            .address_value = *jmprel,
            .size = *pltrelsz,
            .entry_size = kElf32RelEntrySize,
        };
    }
    result.metadata.init_function_address_value = init;
    result.metadata.fini_function_address_value = fini;
    if (init_array.has_value()) {
        result.metadata.init_array = Elf32CollectedFunctionArrayMetadata{
            .address_value = *init_array,
            .size = *init_array_size,
        };
    }
    if (fini_array.has_value()) {
        result.metadata.fini_array = Elf32CollectedFunctionArrayMetadata{
            .address_value = *fini_array,
            .size = *fini_array_size,
        };
    }
    result.metadata.version_symbol_address_value = versym;
    if (verdef.has_value()) {
        result.metadata.version_definition_table =
            Elf32CollectedVersionTableMetadata{
                .address_value = *verdef,
                .count = *verdefnum,
            };
    }
    if (verneed.has_value()) {
        result.metadata.version_requirement_table =
            Elf32CollectedVersionTableMetadata{
                .address_value = *verneed,
                .count = *verneednum,
            };
    }
    result.metadata.flags_1 = flags_1;
    result.metadata.global =
        flags_1.has_value() && ((*flags_1 & kElf32Df1Global) != 0);
    result.metadata.symbolic =
        has_dt_symbolic ||
        (flags.has_value() && ((*flags & kDfSymbolic) != 0));
    result.metadata.has_symbol_versioning =
        versym.has_value() || verdef.has_value() || verneed.has_value();
    result.metadata.soname_offset = soname;
    return result;
}

Elf32LinkerMetadataResult build_elf32_linker_metadata(
    const memory::GuestMemory& memory,
    std::uint32_t load_bias,
    std::span<const Elf32DynamicEntry> entries) {
    const Elf32CollectedLinkerMetadataResult collected =
        collect_elf32_linker_metadata(entries);
    if (!collected) return validation_failure(collected.error);

    Elf32LinkerMetadataResult result;
    result.metadata.soname_offset = collected.metadata.soname_offset;
    result.metadata.needed_offsets = collected.metadata.needed_offsets;
    result.metadata.flags_1 = collected.metadata.flags_1;
    result.metadata.global = collected.metadata.global;
    result.metadata.symbolic = collected.metadata.symbolic;
    result.metadata.has_symbol_versioning =
        collected.metadata.has_symbol_versioning;

    const auto build_legacy_function =
        [&](const std::optional<std::uint32_t>& input,
            std::optional<std::uint32_t>& output)
            -> std::optional<Elf32LinkerMetadataError> {
        if (!input.has_value()) return std::nullopt;
        std::uint32_t guest_address = 0;
        if (!rebase_address(*input, load_bias, guest_address)) {
            return Elf32LinkerMetadataError::AddressOverflow;
        }
        output = guest_address;
        return std::nullopt;
    };

    if (const auto error = build_legacy_function(
            collected.metadata.init_function_address_value,
            result.metadata.init_function)) {
        return validation_failure(*error);
    }
    if (const auto error = build_legacy_function(
            collected.metadata.fini_function_address_value,
            result.metadata.fini_function)) {
        return validation_failure(*error);
    }

    if (const auto& string_table = collected.metadata.string_table) {
        if (result.metadata.soname_offset.has_value() &&
            *result.metadata.soname_offset >= string_table->size) {
            return validation_failure(Elf32LinkerMetadataError::StringOffsetOutOfRange);
        }
        for (const std::uint32_t offset : result.metadata.needed_offsets) {
            if (offset >= string_table->size) {
                return validation_failure(Elf32LinkerMetadataError::StringOffsetOutOfRange);
            }
        }

        std::uint32_t guest_address = 0;
        if (!rebase_address(string_table->address_value, load_bias, guest_address)) {
            return validation_failure(Elf32LinkerMetadataError::AddressOverflow);
        }
        if (!range_fits(guest_address, string_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::RangeOverflow);
        }
        if (!readable_range(memory, guest_address, string_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::ReadFailed);
        }
        result.metadata.string_table = Elf32StringTableMetadata{
            .guest_address = guest_address,
            .size = string_table->size,
        };
    }

    if (const auto& symbol_table = collected.metadata.symbol_table) {
        if (symbol_table->entry_size != kElf32SymbolEntrySize) {
            return validation_failure(Elf32LinkerMetadataError::InvalidSymbolEntrySize);
        }

        std::uint32_t guest_address = 0;
        if (!rebase_address(symbol_table->address_value, load_bias, guest_address)) {
            return validation_failure(Elf32LinkerMetadataError::AddressOverflow);
        }
        if (!range_fits(guest_address, kElf32SymbolEntrySize)) {
            return validation_failure(Elf32LinkerMetadataError::RangeOverflow);
        }
        if (!readable_range(memory, guest_address, kElf32SymbolEntrySize)) {
            return validation_failure(Elf32LinkerMetadataError::ReadFailed);
        }
        result.metadata.symbol_table = Elf32SymbolTableMetadata{
            .guest_address = guest_address,
            .entry_size = symbol_table->entry_size,
        };
    }

    const auto build_hash_descriptor =
        [&](const std::optional<Elf32CollectedHashTableMetadata>& collected_hash,
            std::uint32_t header_size,
            std::optional<Elf32HashTableMetadata>& output)
            -> std::optional<Elf32LinkerMetadataError> {
        if (!collected_hash.has_value()) return std::nullopt;

        std::uint32_t guest_address = 0;
        if (!rebase_address(collected_hash->address_value, load_bias,
                            guest_address)) {
            return Elf32LinkerMetadataError::AddressOverflow;
        }
        if (!range_fits(guest_address, header_size)) {
            return Elf32LinkerMetadataError::RangeOverflow;
        }
        if (!readable_range(memory, guest_address, header_size)) {
            return Elf32LinkerMetadataError::ReadFailed;
        }
        output = Elf32HashTableMetadata{.guest_address = guest_address};
        return std::nullopt;
    };

    if (const auto error =
            build_hash_descriptor(collected.metadata.sysv_hash_table,
                                  kSysvHashHeaderSize,
                                  result.metadata.sysv_hash_table)) {
        return validation_failure(*error);
    }
    if (const auto error =
            build_hash_descriptor(collected.metadata.gnu_hash_table,
                                  kGnuHashHeaderSize,
                                  result.metadata.gnu_hash_table)) {
        return validation_failure(*error);
    }

    if (collected.metadata.version_symbol_address_value.has_value()) {
        std::uint32_t guest_address = 0;
        if (!rebase_address(*collected.metadata.version_symbol_address_value,
                            load_bias, guest_address)) {
            return validation_failure(Elf32LinkerMetadataError::AddressOverflow);
        }
        if (!range_fits(guest_address, 2U)) {
            return validation_failure(Elf32LinkerMetadataError::RangeOverflow);
        }
        if (!readable_range(memory, guest_address, 2U)) {
            return validation_failure(Elf32LinkerMetadataError::ReadFailed);
        }
        result.metadata.version_symbol_table =
            Elf32VersionSymbolTableMetadata{.guest_address = guest_address};
    }

    const auto build_version_table =
        [&](const std::optional<Elf32CollectedVersionTableMetadata>& input,
            std::uint32_t fixed_record_size,
            std::optional<Elf32VersionTableMetadata>& output)
            -> std::optional<Elf32LinkerMetadataError> {
        if (!input.has_value()) return std::nullopt;
        std::uint32_t guest_address = 0;
        if (!rebase_address(input->address_value, load_bias, guest_address)) {
            return Elf32LinkerMetadataError::AddressOverflow;
        }
        if (input->count != 0) {
            if (!range_fits(guest_address, fixed_record_size)) {
                return Elf32LinkerMetadataError::RangeOverflow;
            }
            if (!readable_range(memory, guest_address, fixed_record_size)) {
                return Elf32LinkerMetadataError::ReadFailed;
            }
        }
        output = Elf32VersionTableMetadata{
            .guest_address = guest_address,
            .count = input->count,
        };
        return std::nullopt;
    };

    if (const auto error = build_version_table(
            collected.metadata.version_definition_table, 20U,
            result.metadata.version_definition_table)) {
        return validation_failure(*error);
    }
    if (const auto error = build_version_table(
            collected.metadata.version_requirement_table, 16U,
            result.metadata.version_requirement_table)) {
        return validation_failure(*error);
    }

    const auto build_function_array =
        [&](const std::optional<Elf32CollectedFunctionArrayMetadata>& input,
            std::optional<Elf32FunctionArrayMetadata>& output)
            -> std::optional<Elf32LinkerMetadataError> {
        if (!input.has_value()) return std::nullopt;
        std::uint32_t guest_address = 0;
        if (!rebase_address(input->address_value, load_bias, guest_address)) {
            return Elf32LinkerMetadataError::AddressOverflow;
        }
        if (!range_fits(guest_address, input->size)) {
            return Elf32LinkerMetadataError::RangeOverflow;
        }
        if (!readable_range(memory, guest_address, input->size)) {
            return Elf32LinkerMetadataError::ReadFailed;
        }
        output = Elf32FunctionArrayMetadata{
            .guest_address = guest_address,
            .size = input->size,
        };
        return std::nullopt;
    };

    if (const auto error = build_function_array(
            collected.metadata.init_array, result.metadata.init_array)) {
        return validation_failure(*error);
    }
    if (const auto error = build_function_array(
            collected.metadata.fini_array, result.metadata.fini_array)) {
        return validation_failure(*error);
    }

    if (const auto& rel_table = collected.metadata.rel_table) {
        if (rel_table->entry_size != kElf32RelEntrySize) {
            return validation_failure(Elf32LinkerMetadataError::InvalidRelEntrySize);
        }
        if ((rel_table->size % rel_table->entry_size) != 0) {
            return validation_failure(Elf32LinkerMetadataError::InvalidRelSize);
        }

        std::uint32_t guest_address = 0;
        if (!rebase_address(rel_table->address_value, load_bias, guest_address)) {
            return validation_failure(Elf32LinkerMetadataError::AddressOverflow);
        }
        if (!range_fits(guest_address, rel_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::RangeOverflow);
        }
        if (!readable_range(memory, guest_address, rel_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::ReadFailed);
        }
        result.metadata.rel_table = Elf32RelTableMetadata{
            .guest_address = guest_address,
            .size = rel_table->size,
            .entry_size = rel_table->entry_size,
        };
    }

    if (const auto& plt_rel_table = collected.metadata.plt_rel_table) {
        if ((plt_rel_table->size % kElf32RelEntrySize) != 0) {
            return validation_failure(Elf32LinkerMetadataError::InvalidPltRelSize);
        }

        std::uint32_t guest_address = 0;
        if (!rebase_address(plt_rel_table->address_value, load_bias,
                            guest_address)) {
            return validation_failure(Elf32LinkerMetadataError::AddressOverflow);
        }
        if (!range_fits(guest_address, plt_rel_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::RangeOverflow);
        }
        if (!readable_range(memory, guest_address, plt_rel_table->size)) {
            return validation_failure(Elf32LinkerMetadataError::ReadFailed);
        }
        result.metadata.plt_rel_table = Elf32RelTableMetadata{
            .guest_address = guest_address,
            .size = plt_rel_table->size,
            .entry_size = kElf32RelEntrySize,
        };
    }

    return result;
}

const char* to_string(Elf32LinkerMetadataError error) noexcept {
    switch (error) {
    case Elf32LinkerMetadataError::None: return "none";
    case Elf32LinkerMetadataError::DuplicateSingleton: return "duplicate_singleton";
    case Elf32LinkerMetadataError::IncompleteStringTable: return "incomplete_string_table";
    case Elf32LinkerMetadataError::IncompleteSymbolTable: return "incomplete_symbol_table";
    case Elf32LinkerMetadataError::IncompleteRelTable: return "incomplete_rel_table";
    case Elf32LinkerMetadataError::IncompletePltRelTable: return "incomplete_plt_rel_table";
    case Elf32LinkerMetadataError::IncompleteVersionDefinitionTable: return "incomplete_version_definition_table";
    case Elf32LinkerMetadataError::IncompleteVersionRequirementTable: return "incomplete_version_requirement_table";
    case Elf32LinkerMetadataError::AddressOverflow: return "address_overflow";
    case Elf32LinkerMetadataError::RangeOverflow: return "range_overflow";
    case Elf32LinkerMetadataError::ReadFailed: return "read_failed";
    case Elf32LinkerMetadataError::InvalidSymbolEntrySize: return "invalid_symbol_entry_size";
    case Elf32LinkerMetadataError::InvalidRelEntrySize: return "invalid_rel_entry_size";
    case Elf32LinkerMetadataError::InvalidRelSize: return "invalid_rel_size";
    case Elf32LinkerMetadataError::InvalidPltRelType: return "invalid_plt_rel_type";
    case Elf32LinkerMetadataError::InvalidPltRelSize: return "invalid_plt_rel_size";
    case Elf32LinkerMetadataError::StringOffsetOutOfRange: return "string_offset_out_of_range";
    case Elf32LinkerMetadataError::IncompleteInitArray: return "incomplete_init_array";
    case Elf32LinkerMetadataError::IncompleteFiniArray: return "incomplete_fini_array";
    case Elf32LinkerMetadataError::InvalidFunctionArraySize: return "invalid_function_array_size";
    }
    return "unknown";
}

}  // namespace liba32android::elf
