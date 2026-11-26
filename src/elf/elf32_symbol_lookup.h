#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_linker_metadata.h"
#include "elf/elf32_linker_strings.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

// Caller-selected resource ceilings for symbol/hash processing. Index
// construction uses max_symbols/max_hash_buckets/max_gnu_bloom_words. Later
// lookup/scope operations also use max_scope_objects/max_name_bytes.
// All values are counts/payload bytes, never host addresses.
struct Elf32SymbolLookupOptions {
    std::uint32_t max_symbols{};
    std::uint32_t max_hash_buckets{};
    std::uint32_t max_gnu_bloom_words{};
    std::uint32_t max_scope_objects{};
    std::uint32_t max_name_bytes{};
    // Aggregate ceiling for VERNEED/VERNAUX/VERDEF records when version
    // matching is required. Unversioned objects do not consume this budget.
    std::uint32_t max_version_records{};
    // Ordered caller-owned process/global-group candidates used only by
    // relocation/reference lookup. Plain graph lookup remains graph-local.
    std::span<const std::size_t> global_scope_objects{};
};

struct Elf32SysvHashIndex {
    std::uint32_t bucket_count{};
    std::uint32_t chain_count{};
    std::uint32_t buckets_guest_address{};
    std::uint32_t chains_guest_address{};
};

struct Elf32GnuHashIndex {
    std::uint32_t bucket_count{};
    std::uint32_t symbol_offset{};
    std::uint32_t bloom_word_count{};
    std::uint32_t bloom_shift{};
    std::uint32_t bloom_guest_address{};
    std::uint32_t buckets_guest_address{};
    std::uint32_t chains_guest_address{};
};

struct Elf32SymbolIndex {
    // Exact dynamic-symbol count established from DT_HASH nchain or from a
    // bounded terminating GNU hash chain when GNU hash is the only format.
    std::uint32_t symbol_count{};
    std::optional<Elf32SysvHashIndex> sysv_hash;
    std::optional<Elf32GnuHashIndex> gnu_hash;
};

enum class Elf32SymbolIndexError : std::uint8_t {
    None = 0,
    InvalidOptions,
    MissingStringTable,
    MissingSymbolTable,
    MissingHashTable,
    HashHeaderReadFailed,
    HashRangeOverflow,
    HashReadFailed,
    InvalidSysvHash,
    InvalidGnuHash,
    HashLimitExceeded,
    HashIndexOutOfRange,
    UnterminatedGnuChain,
    SymbolCountExceeded,
    SymbolRangeOverflow,
    SymbolReadFailed,
};

struct Elf32SymbolIndexResult {
    Elf32SymbolIndexError error{Elf32SymbolIndexError::None};
    Elf32SymbolIndex index;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32SymbolIndexError::None;
    }
};

struct Elf32Symbol {
    std::uint32_t name_offset{};
    std::uint32_t value{};
    std::uint32_t size{};
    std::uint8_t binding{};
    std::uint8_t type{};
    std::uint8_t visibility{};
    std::uint8_t raw_other{};
    std::uint16_t section_index{};
};

enum class Elf32SymbolReadError : std::uint8_t {
    None = 0,
    InvalidMetadata,
    SymbolIndexOutOfRange,
    SymbolReadFailed,
};

struct Elf32SymbolReadResult {
    Elf32SymbolReadError error{Elf32SymbolReadError::None};
    Elf32Symbol symbol;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32SymbolReadError::None;
    }
};

struct Elf32ResolvedSymbol {
    std::uint32_t symbol_index{};
    std::string name;
    Elf32Symbol symbol;
    // Logical 32-bit guest value. This is never a host pointer.
    std::uint32_t guest_value{};
};

enum class Elf32SymbolLookupError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidMetadata,
    InvalidLookupName,
    UnsupportedVersioning,
    VersionReadFailed,
    InvalidVersionMetadata,
    VersionRecordLimitExceeded,
    VersionIndexNotFound,
    VersionDependencyNotFound,
    HashReadFailed,
    HashIndexOutOfRange,
    InvalidHashChain,
    SymbolReadFailed,
    StringReadFailed,
    SymbolNotFound,
    UnsupportedBinding,
    UnsupportedType,
    UnsupportedVisibility,
    UnsupportedSectionIndex,
    ValueOverflow,
};

struct Elf32ObjectSymbolLookupResult {
    Elf32SymbolLookupError error{Elf32SymbolLookupError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};
    Elf32ResolvedSymbol symbol;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32SymbolLookupError::None;
    }
};

struct Elf32GraphResolvedSymbol {
    std::size_t object_index{};
    Elf32ResolvedSymbol symbol;
};

enum class Elf32GraphSymbolLookupError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidGraphStart,
    InvalidGraphEdge,
    InvalidGlobalScopeObject,
    ScopeLimitExceeded,
    IndexBuildFailed,
    ObjectLookupFailed,
    SymbolNotFound,
};

struct Elf32GraphSymbolLookupResult {
    Elf32GraphSymbolLookupError error{Elf32GraphSymbolLookupError::None};
    std::optional<std::size_t> failing_object;
    Elf32SymbolIndexError index_error{Elf32SymbolIndexError::None};
    Elf32SymbolLookupError lookup_error{Elf32SymbolLookupError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};
    Elf32GraphResolvedSymbol symbol;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32GraphSymbolLookupError::None;
    }
};

// Build a read-only, bounded index for one already-loaded object's dynamic
// symbol table. No section headers or host pointers are used. The function
// reads only through GuestMemory, never mutates guest bytes/mappings, and
// validates the complete symbol-table range implied by the selected hash
// format before returning success.
[[nodiscard]] Elf32SymbolIndexResult build_elf32_symbol_index(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolLookupOptions& options);

// Decode one entry from an already-bounded dynamic-symbol extent.
// No host pointer is exposed and guest memory is not modified.
[[nodiscard]] Elf32SymbolReadResult read_elf32_symbol_entry(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolIndex& index,
    std::uint32_t symbol_index);

// Resolve one exact non-empty byte name within one already-indexed object.
// GNU hash is preferred when both formats exist; a GNU miss is not masked by
// SysV fallback. GLOBAL/WEAK DEFAULT/PROTECTED normal or SHN_ABS definitions
// are supported. LOCAL/UNDEF/HIDDEN/INTERNAL entries are ordinary misses.
// Matching TLS/COMMON/XINDEX/IFUNC/nonstandard semantics fail explicitly.
// Versioned provider candidates are filtered by the bounded feature-014
// version layer; this API itself carries no explicit requester version.
// The operation is read-only and uses only logical guest addresses.
[[nodiscard]] Elf32ObjectSymbolLookupResult lookup_elf32_symbol(
    const memory::GuestMemory& memory,
    std::uint32_t load_bias,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolIndex& index,
    std::string_view name,
    const Elf32SymbolLookupOptions& options);

// Search one graph-local breadth-first dependency closure. The start object is
// visited first, then direct/transitive dependency targets in stored edge
// order, each object at most once. Object-vector discovery order is not a
// lookup scope. The first eligible definition wins, including an earlier weak
// definition. Objects without SYMTAB are skipped; malformed searchable
// objects fail instead of being skipped. Versioned objects are eligible and
// unversioned lookup ignores hidden definitions. The operation is read-only
// and bounded by max_scope_objects.
[[nodiscard]] Elf32GraphSymbolLookupResult lookup_elf32_graph_symbol(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t start_object,
    std::string_view name,
    const Elf32SymbolLookupOptions& options);

// Resolve one relocation/reference symbol using its requester's DT_VERSYM
// entry. Indices 0/1 behave as unversioned requests; higher indices are
// resolved through bounded VERNEED/VERDEF metadata. If global_scope_objects
// is non-empty, ordinary requesters search that ordered scope before their
// graph-local BFS closure. DT_SYMBOLIC/DF_SYMBOLIC requesters search themselves
// first, then the explicit global scope, then the remaining local closure.
// Every unique searched object consumes the same max_scope_objects budget.
[[nodiscard]] Elf32GraphSymbolLookupResult
lookup_elf32_graph_symbol_for_reference(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t start_object,
    std::uint32_t reference_symbol_index,
    std::string_view name,
    const Elf32SymbolLookupOptions& options);

[[nodiscard]] const char* to_string(Elf32SymbolIndexError error) noexcept;
[[nodiscard]] const char* to_string(Elf32SymbolReadError error) noexcept;
[[nodiscard]] const char* to_string(Elf32SymbolLookupError error) noexcept;
[[nodiscard]] const char* to_string(Elf32GraphSymbolLookupError error) noexcept;

}  // namespace liba32android::elf
