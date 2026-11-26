#pragma once

#include <cstdint>
#include <optional>

#include "elf/elf32_load_types.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32RelroOptions {
    // Maximum total page occurrences declared by all RELRO descriptors before
    // overlap deduplication. Zero is valid only when no RELRO metadata exists.
    std::uint32_t max_pages{};
};

enum class Elf32RelroError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidMetadata,
    PageLimitExceeded,
    UnmappedPage,
    UnreadablePage,
    ExecutablePage,
    ProtectFailed,
    RollbackFailed,
};

struct Elf32RelroSealResult {
    Elf32RelroError error{Elf32RelroError::None};
    // Equals error except when error == RollbackFailed, where primary_error
    // preserves ProtectFailed.
    Elf32RelroError primary_error{Elf32RelroError::None};
    std::optional<std::uint32_t> failing_page;
    std::optional<std::uint32_t> rollback_failing_page;
    // Number of unique RELRO pages on complete success. Failure publishes zero.
    std::uint32_t sealed_pages{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32RelroError::None;
    }
};

// Seal one already-loaded ELF32 object's validated GNU RELRO ranges read-only.
//
// The loader intentionally leaves RELRO writable so relocations can run first.
// Callers must therefore invoke this only after all relocation writes needed by
// the object have completed.
//
// The complete descriptor/page/permission set is preflighted before the first
// protection change. Only mapped readable non-executable pages are accepted.
// RW pages become R; already-R pages are idempotent. On a later protect failure,
// earlier changes are restored in reverse order when possible.
[[nodiscard]] Elf32RelroSealResult seal_elf32_gnu_relro(
    memory::MappedGuestMemory& memory,
    const Elf32LoadResult& load,
    const Elf32RelroOptions& options);

[[nodiscard]] const char* to_string(Elf32RelroError error) noexcept;

}  // namespace liba32android::elf
