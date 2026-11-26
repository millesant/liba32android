#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_linker_strings.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32SymbolVersionOptions {
    std::uint32_t max_records{};
    std::uint32_t max_name_bytes{};
};

enum class Elf32SymbolVersionError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidMetadata,
    ReadFailed,
    StringReadFailed,
    InvalidRecord,
    RecordLimitExceeded,
    VersionIndexNotFound,
    DependencyNotFound,
};

struct Elf32SymbolVersionRequirement {
    std::uint32_t elf_hash{};
    std::string name;
    std::optional<std::size_t> target_object;
};

struct Elf32SymbolVersionRequirementResult {
    Elf32SymbolVersionError error{Elf32SymbolVersionError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};
    std::optional<Elf32SymbolVersionRequirement> requirement;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32SymbolVersionError::None;
    }
};

struct Elf32SymbolVersionMatchResult {
    Elf32SymbolVersionError error{Elf32SymbolVersionError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};
    bool matches{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32SymbolVersionError::None;
    }
};

// Derive the version requested by one requester-side dynamic symbol. A missing
// DT_VERSYM table or version indices 0/1 means no explicit version request.
// Higher indices must resolve through bounded VERNEED/VERDEF records.
[[nodiscard]] Elf32SymbolVersionRequirementResult
resolve_elf32_symbol_version_requirement(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t requester_object,
    std::uint32_t symbol_index,
    const Elf32SymbolVersionOptions& options);

// Test one provider-side candidate against an optional version request.
// Unversioned requests reject hidden (0x8000) definitions. Explicit requests
// match a provider VERDEF name/hash when present and otherwise use global
// version index 1, matching Android/bionic behavior.
[[nodiscard]] Elf32SymbolVersionMatchResult match_elf32_symbol_version(
    const memory::GuestMemory& memory,
    const Elf32LoadedDependencyObject& object,
    std::uint32_t symbol_index,
    const std::optional<Elf32SymbolVersionRequirement>& requirement,
    const Elf32SymbolVersionOptions& options);

[[nodiscard]] const char* to_string(Elf32SymbolVersionError error) noexcept;

}  // namespace liba32android::elf
