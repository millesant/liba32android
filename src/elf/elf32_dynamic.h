#pragma once

#include <cstdint>
#include <vector>

#include "elf/elf32_load_types.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

enum class Elf32DynamicError : std::uint8_t {
    None = 0,
    InvalidRange,
    TruncatedEntry,
    ReadFailed,
    Unterminated,
};

struct Elf32DynamicEntry {
    // Raw ELF32 d_tag/d_val pair. Pointer-like values are deliberately not
    // rebased or dereferenced in this structural parsing layer.
    std::int32_t tag{};
    std::uint32_t value{};
};

struct Elf32DynamicResult {
    Elf32DynamicError error{Elf32DynamicError::None};
    // Includes the first DT_NULL entry. Bytes after that terminator are not
    // part of the logical dynamic array and are deliberately ignored.
    std::vector<Elf32DynamicEntry> entries;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32DynamicError::None;
    }
};

// Parse the file-backed bytes of a loader-validated PT_DYNAMIC range from guest
// memory. This function is structural only: it does not interpret tag
// semantics, dereference guest pointers, resolve dependencies/symbols, or apply
// relocations.
[[nodiscard]] Elf32DynamicResult parse_elf32_dynamic(
    const memory::GuestMemory& memory,
    const Elf32DynamicSegment& segment);

[[nodiscard]] const char* to_string(Elf32DynamicError error) noexcept;

}  // namespace liba32android::elf
