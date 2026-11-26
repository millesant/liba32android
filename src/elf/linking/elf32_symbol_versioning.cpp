#include "elf/elf32_symbol_versioning.h"

#include "elf/internal/elf32_bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace liba32android::elf {
namespace {

constexpr std::uint16_t kVersionCurrent = 1;
constexpr std::uint16_t kVersionIndexMask = 0x7fffU;
constexpr std::uint16_t kVersionHidden = 0x8000U;
constexpr std::uint16_t kVersionGlobal = 1;
constexpr std::uint16_t kVersionFlagBase = 0x1U;

constexpr std::size_t kVerneedSize = 16;
constexpr std::size_t kVernauxSize = 16;
constexpr std::size_t kVerdefSize = 20;
constexpr std::size_t kVerdauxSize = 8;

struct WalkBudget {
    std::uint32_t used{};
    std::uint32_t limit{};

    [[nodiscard]] bool take() noexcept {
        if (used >= limit) return false;
        ++used;
        return true;
    }
};

[[nodiscard]] Elf32SymbolVersionRequirementResult requirement_failure(
    Elf32SymbolVersionError error,
    Elf32LinkerStringError string_error = Elf32LinkerStringError::None) {
    Elf32SymbolVersionRequirementResult result;
    result.error = error;
    result.string_error = string_error;
    return result;
}

[[nodiscard]] Elf32SymbolVersionMatchResult match_failure(
    Elf32SymbolVersionError error,
    Elf32LinkerStringError string_error = Elf32LinkerStringError::None) {
    Elf32SymbolVersionMatchResult result;
    result.error = error;
    result.string_error = string_error;
    return result;
}

template <std::size_t Size>
[[nodiscard]] bool read_record(const memory::GuestMemory& memory,
                               std::uint32_t address,
                               std::array<std::uint8_t, Size>& bytes) {
    return memory.read(address, bytes);
}

[[nodiscard]] bool add_address(std::uint32_t base,
                               std::uint32_t offset,
                               std::uint32_t& output) {
    return detail::checked_add_guest_address(base, offset, output);
}

[[nodiscard]] bool has_any_version_descriptor(
    const Elf32LinkerMetadata& metadata) noexcept {
    return metadata.version_symbol_table.has_value() ||
           metadata.version_definition_table.has_value() ||
           metadata.version_requirement_table.has_value();
}

[[nodiscard]] bool read_versym(const memory::GuestMemory& memory,
                               const Elf32VersionSymbolTableMetadata& table,
                               std::uint32_t symbol_index,
                               std::uint16_t& value) {
    std::uint32_t address = 0;
    if (!detail::checked_add_guest_address(
            table.guest_address,
            static_cast<std::uint64_t>(symbol_index) * 2U,
            address)) {
        return false;
    }
    std::array<std::uint8_t, 2> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = detail::decode_u16_le(bytes.data());
    return true;
}

[[nodiscard]] std::optional<std::size_t> dependency_for_soname(
    const Elf32DependencyGraph& graph,
    const Elf32LoadedDependencyObject& requester,
    std::string_view soname,
    bool& invalid_graph) {
    invalid_graph = false;
    for (const Elf32DependencyEdge& edge : requester.dependencies) {
        if (edge.target_object >= graph.objects.size()) {
            invalid_graph = true;
            return std::nullopt;
        }
        const auto& target = graph.objects[edge.target_object];
        if (target.linker_strings.soname.has_value() &&
            *target.linker_strings.soname == soname) {
            return edge.target_object;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool valid_walk_options(
    const Elf32SymbolVersionOptions& options) noexcept {
    return options.max_records != 0 && options.max_name_bytes != 0;
}

}  // namespace

Elf32SymbolVersionRequirementResult resolve_elf32_symbol_version_requirement(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t requester_object,
    std::uint32_t symbol_index,
    const Elf32SymbolVersionOptions& options) {
    if (requester_object >= graph.objects.size()) {
        return requirement_failure(Elf32SymbolVersionError::InvalidMetadata);
    }
    const auto& requester = graph.objects[requester_object];
    const auto& metadata = requester.linker_metadata;

    if (metadata.has_symbol_versioning &&
        !has_any_version_descriptor(metadata)) {
        return requirement_failure(Elf32SymbolVersionError::InvalidMetadata);
    }
    if (!metadata.version_symbol_table.has_value()) {
        return {};
    }

    std::uint16_t raw_version = 0;
    if (!read_versym(memory, *metadata.version_symbol_table,
                     symbol_index, raw_version)) {
        return requirement_failure(Elf32SymbolVersionError::ReadFailed);
    }
    const std::uint16_t source_index =
        static_cast<std::uint16_t>(raw_version & kVersionIndexMask);
    if (source_index <= kVersionGlobal) {
        return {};
    }
    if (!valid_walk_options(options) ||
        !metadata.string_table.has_value()) {
        return requirement_failure(
            options.max_records == 0 || options.max_name_bytes == 0
                ? Elf32SymbolVersionError::InvalidOptions
                : Elf32SymbolVersionError::InvalidMetadata);
    }

    WalkBudget budget{.limit = options.max_records};
    std::optional<Elf32SymbolVersionRequirement> found;

    if (metadata.version_requirement_table.has_value()) {
        const auto& table = *metadata.version_requirement_table;
        std::uint32_t current = table.guest_address;
        for (std::uint32_t i = 0; i < table.count; ++i) {
            if (!budget.take()) {
                return requirement_failure(
                    Elf32SymbolVersionError::RecordLimitExceeded);
            }
            std::array<std::uint8_t, kVerneedSize> record{};
            if (!read_record(memory, current, record)) {
                return requirement_failure(Elf32SymbolVersionError::ReadFailed);
            }
            const std::uint16_t version = detail::decode_u16_le(record.data());
            const std::uint16_t aux_count =
                detail::decode_u16_le(record.data() + 2);
            const std::uint32_t file_offset =
                detail::decode_u32_le(record.data() + 4);
            const std::uint32_t aux_offset =
                detail::decode_u32_le(record.data() + 8);
            const std::uint32_t next_offset =
                detail::decode_u32_le(record.data() + 12);
            if (version != kVersionCurrent || aux_count == 0 ||
                aux_offset == 0) {
                return requirement_failure(
                    Elf32SymbolVersionError::InvalidRecord);
            }

            const auto target_name = read_elf32_string_table_entry(
                memory, *metadata.string_table, file_offset,
                Elf32LinkerStringOptions{
                    .max_string_bytes = options.max_name_bytes,
                });
            if (!target_name) {
                return requirement_failure(
                    Elf32SymbolVersionError::StringReadFailed,
                    target_name.error);
            }
            bool invalid_graph = false;
            const auto target_object = dependency_for_soname(
                graph, requester, target_name.value, invalid_graph);
            if (invalid_graph) {
                return requirement_failure(
                    Elf32SymbolVersionError::InvalidMetadata);
            }
            if (!target_object.has_value()) {
                return requirement_failure(
                    Elf32SymbolVersionError::DependencyNotFound);
            }

            std::uint32_t aux = 0;
            if (!add_address(current, aux_offset, aux)) {
                return requirement_failure(
                    Elf32SymbolVersionError::InvalidRecord);
            }
            for (std::uint16_t j = 0; j < aux_count; ++j) {
                if (!budget.take()) {
                    return requirement_failure(
                        Elf32SymbolVersionError::RecordLimitExceeded);
                }
                std::array<std::uint8_t, kVernauxSize> aux_record{};
                if (!read_record(memory, aux, aux_record)) {
                    return requirement_failure(
                        Elf32SymbolVersionError::ReadFailed);
                }
                const std::uint32_t elf_hash =
                    detail::decode_u32_le(aux_record.data());
                const std::uint16_t other =
                    detail::decode_u16_le(aux_record.data() + 6);
                const std::uint32_t name_offset =
                    detail::decode_u32_le(aux_record.data() + 8);
                const std::uint32_t aux_next =
                    detail::decode_u32_le(aux_record.data() + 12);

                const auto version_name = read_elf32_string_table_entry(
                    memory, *metadata.string_table, name_offset,
                    Elf32LinkerStringOptions{
                        .max_string_bytes = options.max_name_bytes,
                    });
                if (!version_name) {
                    return requirement_failure(
                        Elf32SymbolVersionError::StringReadFailed,
                        version_name.error);
                }
                if ((other & kVersionIndexMask) == source_index) {
                    found = Elf32SymbolVersionRequirement{
                        .elf_hash = elf_hash,
                        .name = version_name.value,
                        .target_object = *target_object,
                    };
                }

                if (j + 1U < aux_count) {
                    if (aux_next == 0 || !add_address(aux, aux_next, aux)) {
                        return requirement_failure(
                            Elf32SymbolVersionError::InvalidRecord);
                    }
                }
            }

            if (i + 1U < table.count) {
                if (next_offset == 0 ||
                    !add_address(current, next_offset, current)) {
                    return requirement_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
            }
        }
    }

    // VERDEF is processed after VERNEED, matching bionic's VersionTracker:
    // a same-index local definition, when present, supersedes a requirement.
    if (metadata.version_definition_table.has_value()) {
        const auto& table = *metadata.version_definition_table;
        std::uint32_t current = table.guest_address;
        for (std::uint32_t i = 0; i < table.count; ++i) {
            if (!budget.take()) {
                return requirement_failure(
                    Elf32SymbolVersionError::RecordLimitExceeded);
            }
            std::array<std::uint8_t, kVerdefSize> record{};
            if (!read_record(memory, current, record)) {
                return requirement_failure(Elf32SymbolVersionError::ReadFailed);
            }
            const std::uint16_t version = detail::decode_u16_le(record.data());
            const std::uint16_t flags =
                detail::decode_u16_le(record.data() + 2);
            const std::uint16_t index =
                detail::decode_u16_le(record.data() + 4);
            const std::uint16_t aux_count =
                detail::decode_u16_le(record.data() + 6);
            const std::uint32_t elf_hash =
                detail::decode_u32_le(record.data() + 8);
            const std::uint32_t aux_offset =
                detail::decode_u32_le(record.data() + 12);
            const std::uint32_t next_offset =
                detail::decode_u32_le(record.data() + 16);
            if (version != kVersionCurrent) {
                return requirement_failure(
                    Elf32SymbolVersionError::InvalidRecord);
            }

            if ((flags & kVersionFlagBase) == 0) {
                if (aux_count == 0 || aux_offset == 0) {
                    return requirement_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
                if (!budget.take()) {
                    return requirement_failure(
                        Elf32SymbolVersionError::RecordLimitExceeded);
                }
                std::uint32_t aux = 0;
                if (!add_address(current, aux_offset, aux)) {
                    return requirement_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
                std::array<std::uint8_t, kVerdauxSize> aux_record{};
                if (!read_record(memory, aux, aux_record)) {
                    return requirement_failure(
                        Elf32SymbolVersionError::ReadFailed);
                }
                const std::uint32_t name_offset =
                    detail::decode_u32_le(aux_record.data());
                const auto version_name = read_elf32_string_table_entry(
                    memory, *metadata.string_table, name_offset,
                    Elf32LinkerStringOptions{
                        .max_string_bytes = options.max_name_bytes,
                    });
                if (!version_name) {
                    return requirement_failure(
                        Elf32SymbolVersionError::StringReadFailed,
                        version_name.error);
                }
                if ((index & kVersionIndexMask) == source_index) {
                    found = Elf32SymbolVersionRequirement{
                        .elf_hash = elf_hash,
                        .name = version_name.value,
                        .target_object = requester_object,
                    };
                }
            }

            if (i + 1U < table.count) {
                if (next_offset == 0 ||
                    !add_address(current, next_offset, current)) {
                    return requirement_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
            }
        }
    }

    if (!found.has_value()) {
        return requirement_failure(
            Elf32SymbolVersionError::VersionIndexNotFound);
    }
    Elf32SymbolVersionRequirementResult result;
    result.requirement = std::move(found);
    return result;
}

Elf32SymbolVersionMatchResult match_elf32_symbol_version(
    const memory::GuestMemory& memory,
    const Elf32LoadedDependencyObject& object,
    std::uint32_t symbol_index,
    const std::optional<Elf32SymbolVersionRequirement>& requirement,
    const Elf32SymbolVersionOptions& options) {
    const auto& metadata = object.linker_metadata;
    if (metadata.has_symbol_versioning &&
        !has_any_version_descriptor(metadata)) {
        return match_failure(Elf32SymbolVersionError::InvalidMetadata);
    }

    if (!metadata.version_symbol_table.has_value()) {
        return Elf32SymbolVersionMatchResult{
            .matches = true,
        };
    }

    std::uint16_t raw_version = 0;
    if (!read_versym(memory, *metadata.version_symbol_table,
                     symbol_index, raw_version)) {
        return match_failure(Elf32SymbolVersionError::ReadFailed);
    }

    if (!requirement.has_value()) {
        return Elf32SymbolVersionMatchResult{
            .matches = (raw_version & kVersionHidden) == 0,
        };
    }

    std::uint16_t expected_index = kVersionGlobal;
    if (metadata.version_definition_table.has_value()) {
        if (!valid_walk_options(options) ||
            !metadata.string_table.has_value()) {
            return match_failure(
                options.max_records == 0 || options.max_name_bytes == 0
                    ? Elf32SymbolVersionError::InvalidOptions
                    : Elf32SymbolVersionError::InvalidMetadata);
        }

        WalkBudget budget{.limit = options.max_records};
        const auto& table = *metadata.version_definition_table;
        std::uint32_t current = table.guest_address;
        for (std::uint32_t i = 0; i < table.count; ++i) {
            if (!budget.take()) {
                return match_failure(
                    Elf32SymbolVersionError::RecordLimitExceeded);
            }
            std::array<std::uint8_t, kVerdefSize> record{};
            if (!read_record(memory, current, record)) {
                return match_failure(Elf32SymbolVersionError::ReadFailed);
            }
            const std::uint16_t version = detail::decode_u16_le(record.data());
            const std::uint16_t flags =
                detail::decode_u16_le(record.data() + 2);
            const std::uint16_t index =
                detail::decode_u16_le(record.data() + 4);
            const std::uint16_t aux_count =
                detail::decode_u16_le(record.data() + 6);
            const std::uint32_t elf_hash =
                detail::decode_u32_le(record.data() + 8);
            const std::uint32_t aux_offset =
                detail::decode_u32_le(record.data() + 12);
            const std::uint32_t next_offset =
                detail::decode_u32_le(record.data() + 16);
            if (version != kVersionCurrent) {
                return match_failure(
                    Elf32SymbolVersionError::InvalidRecord);
            }

            if ((flags & kVersionFlagBase) == 0) {
                if (aux_count == 0 || aux_offset == 0) {
                    return match_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
                if (!budget.take()) {
                    return match_failure(
                        Elf32SymbolVersionError::RecordLimitExceeded);
                }
                std::uint32_t aux = 0;
                if (!add_address(current, aux_offset, aux)) {
                    return match_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
                std::array<std::uint8_t, kVerdauxSize> aux_record{};
                if (!read_record(memory, aux, aux_record)) {
                    return match_failure(
                        Elf32SymbolVersionError::ReadFailed);
                }
                const std::uint32_t name_offset =
                    detail::decode_u32_le(aux_record.data());
                const auto version_name = read_elf32_string_table_entry(
                    memory, *metadata.string_table, name_offset,
                    Elf32LinkerStringOptions{
                        .max_string_bytes = options.max_name_bytes,
                    });
                if (!version_name) {
                    return match_failure(
                        Elf32SymbolVersionError::StringReadFailed,
                        version_name.error);
                }
                if (elf_hash == requirement->elf_hash &&
                    version_name.value == requirement->name) {
                    expected_index =
                        static_cast<std::uint16_t>(
                            index & kVersionIndexMask);
                }
            }

            if (i + 1U < table.count) {
                if (next_offset == 0 ||
                    !add_address(current, next_offset, current)) {
                    return match_failure(
                        Elf32SymbolVersionError::InvalidRecord);
                }
            }
        }
    }

    return Elf32SymbolVersionMatchResult{
        .matches =
            static_cast<std::uint16_t>(raw_version & kVersionIndexMask) ==
            expected_index,
    };
}

const char* to_string(Elf32SymbolVersionError error) noexcept {
    switch (error) {
    case Elf32SymbolVersionError::None: return "none";
    case Elf32SymbolVersionError::InvalidOptions: return "invalid_options";
    case Elf32SymbolVersionError::InvalidMetadata: return "invalid_metadata";
    case Elf32SymbolVersionError::ReadFailed: return "read_failed";
    case Elf32SymbolVersionError::StringReadFailed: return "string_read_failed";
    case Elf32SymbolVersionError::InvalidRecord: return "invalid_record";
    case Elf32SymbolVersionError::RecordLimitExceeded: return "record_limit_exceeded";
    case Elf32SymbolVersionError::VersionIndexNotFound: return "version_index_not_found";
    case Elf32SymbolVersionError::DependencyNotFound: return "dependency_not_found";
    }
    return "unknown";
}

}  // namespace liba32android::elf
