#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "elf/elf32_linker_metadata.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

enum class Elf32LinkerStringError : std::uint8_t {
    None = 0,
    MissingStringTable,
    StringOffsetOutOfRange,
    AddressOverflow,
    ReadFailed,
    UnterminatedString,
    StringTooLong,
};

struct Elf32LinkerStringOptions {
    // Maximum number of non-NUL payload bytes that may be materialized.
    // The terminating NUL may appear immediately after this many bytes.
    std::uint32_t max_string_bytes{};
};

struct Elf32SingleStringResult {
    Elf32LinkerStringError error{Elf32LinkerStringError::None};
    std::string value;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LinkerStringError::None;
    }
};

struct Elf32LinkerStrings {
    std::optional<std::string> soname;
    std::vector<std::string> needed;
};

struct Elf32LinkerStringResult {
    Elf32LinkerStringError error{Elf32LinkerStringError::None};
    Elf32LinkerStrings strings;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32LinkerStringError::None;
    }
};

// Read one NUL-terminated byte string from a validated ELF32 string-table
// descriptor. The function is read-only, uses GuestMemory exclusively, and
// requires an explicit per-string payload ceiling.
[[nodiscard]] Elf32SingleStringResult read_elf32_string_table_entry(
    const memory::GuestMemory& memory,
    const Elf32StringTableMetadata& string_table,
    std::uint32_t offset,
    const Elf32LinkerStringOptions& options);

// Materialize SONAME and ordered DT_NEEDED names from validated linker
// metadata. Success is all-or-nothing: no partial aggregate is returned when
// any requested string fails.
[[nodiscard]] Elf32LinkerStringResult build_elf32_linker_strings(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32LinkerStringOptions& options);

[[nodiscard]] const char* to_string(Elf32LinkerStringError error) noexcept;

}  // namespace liba32android::elf
