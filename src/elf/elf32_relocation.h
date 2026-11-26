#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

inline constexpr std::uint8_t kRArmNone = 0;
inline constexpr std::uint8_t kRArmAbs32 = 2;
inline constexpr std::uint8_t kRArmRel32 = 3;
inline constexpr std::uint8_t kRArmGlobDat = 21;
inline constexpr std::uint8_t kRArmJumpSlot = 22;
inline constexpr std::uint8_t kRArmRelative = 23;

enum class Elf32RelocationTable : std::uint8_t {
    MainRel = 0,
    PltRel,
};

// Caller-selected bounds for one relocation-table operation. max_relocations
// applies independently to the selected main REL or PLT REL table. Symbol
// limits are shared by the reference-resolution/application stages.
struct Elf32RelocationOptions {
    std::uint32_t max_relocations{};
    Elf32SymbolLookupOptions symbols{};
};

struct Elf32RelocationEntry {
    std::uint32_t index{};
    std::uint32_t offset{};
    std::uint32_t info{};
    std::uint32_t symbol_index{};
    std::uint8_t type{};
    // Checked logical guest address: object load_bias + r_offset.
    // This is never a host pointer.
    std::uint32_t place_guest_address{};
    // Present only for supported write-producing relocations. This is the
    // original little-endian 32-bit place word: the REL addend where the
    // relocation formula uses A, and later the rollback snapshot.
    std::optional<std::uint32_t> original_word;
};

struct Elf32RelocationPlan {
    std::size_t object_index{};
    std::vector<Elf32RelocationEntry> entries;
};

enum class Elf32RelocationPlanError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidGraphObject,
    TooManyRelocations,
    RelocationReadFailed,
    PlaceOverflow,
    UnalignedPlace,
    TargetReadFailed,
    DuplicateTarget,
    UnsupportedRelocationType,
};

struct Elf32RelocationPlanResult {
    Elf32RelocationPlanError error{Elf32RelocationPlanError::None};
    Elf32RelocationPlan plan;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32RelocationPlanError::None;
    }
};

struct Elf32RelocationReference {
    std::uint32_t symbol_index{};
    std::string name;
    Elf32Symbol symbol;
    // Defining symbol metadata when graph lookup succeeds. This is distinct
    // from symbol, which is the relocating object's reference symbol.
    std::optional<Elf32Symbol> defining_symbol;
    std::uint32_t symbol_value{};
    bool unresolved_weak{};
    std::optional<std::size_t> defining_object_index;
    std::optional<std::uint32_t> defining_symbol_index;
};

struct Elf32ResolvedRelocationEntry {
    Elf32RelocationEntry relocation;
    std::optional<Elf32RelocationReference> reference;
};

struct Elf32RelocationResolution {
    std::size_t object_index{};
    std::vector<Elf32ResolvedRelocationEntry> entries;
};

enum class Elf32RelocationResolveError : std::uint8_t {
    None = 0,
    InvalidOptions,
    PlanFailed,
    IndexBuildFailed,
    MissingReferenceSymbol,
    SymbolIndexOutOfRange,
    ReferenceSymbolReadFailed,
    ReferenceNameFailed,
    UnsupportedVersioning,
    UnsupportedReferenceBinding,
    UnsupportedReferenceVisibility,
    UnsupportedReferenceType,
    UnsupportedReferenceSection,
    SymbolLookupFailed,
    UnresolvedStrongSymbol,
};

struct Elf32RelocationResolutionResult {
    Elf32RelocationResolveError error{Elf32RelocationResolveError::None};
    Elf32RelocationPlanError plan_error{Elf32RelocationPlanError::None};
    Elf32SymbolIndexError index_error{Elf32SymbolIndexError::None};
    Elf32SymbolReadError symbol_read_error{Elf32SymbolReadError::None};
    Elf32LinkerStringError string_error{Elf32LinkerStringError::None};
    Elf32GraphSymbolLookupError graph_error{Elf32GraphSymbolLookupError::None};
    Elf32SymbolLookupError lookup_error{Elf32SymbolLookupError::None};
    std::optional<std::size_t> failing_object;
    std::optional<std::uint32_t> failing_relocation;
    Elf32RelocationResolution resolution;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32RelocationResolveError::None;
    }
};

struct Elf32RelocationWrite {
    std::uint32_t relocation_index{};
    std::uint8_t type{};
    std::uint32_t place_guest_address{};
    std::uint32_t original_word{};
    std::uint32_t final_word{};
    Elf32RelocationTable table{Elf32RelocationTable::MainRel};
};

struct Elf32RelocationApplication {
    std::size_t object_index{};
    std::vector<Elf32RelocationWrite> writes;
};

enum class Elf32RelocationApplyError : std::uint8_t {
    None = 0,
    ResolveFailed,
    InvalidRelativeSymbol,
    InvalidResolvedEntry,
    DuplicateTargetAcrossTables,
    TargetWriteFailed,
    RollbackFailed,
};

struct Elf32RelocationApplyResult {
    Elf32RelocationApplyError error{Elf32RelocationApplyError::None};
    // Equals the direct failure except when error == RollbackFailed, where
    // primary_error preserves the original TargetWriteFailed cause.
    Elf32RelocationApplyError primary_error{Elf32RelocationApplyError::None};
    Elf32RelocationResolutionResult resolution_failure;
    std::optional<std::uint32_t> failing_relocation;
    std::optional<std::uint32_t> rollback_failing_relocation;
    std::optional<Elf32RelocationTable> failing_table;
    std::optional<Elf32RelocationTable> rollback_failing_table;
    // Populated only on complete success. Failure never publishes a partial
    // successful application.
    Elf32RelocationApplication application;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32RelocationApplyError::None;
    }
};

// Decode and validate one loaded object's main DT_REL table without mutating
// guest memory. The table descriptor is assumed to originate from validated
// Elf32LinkerMetadata (DT_RELENT == 8 and DT_RELSZ divisible by 8), but all
// guest bytes are still decoded explicitly through GuestMemory.
//
// Supported plan types are R_ARM_NONE, R_ARM_ABS32, R_ARM_REL32,
// R_ARM_GLOB_DAT and R_ARM_RELATIVE. Write-producing entries require a word-aligned, readable
// 32-bit place and capture its original word. R_ARM_NONE never reads its
// target. Duplicate write-producing places are rejected so future
// plan-before-mutate application never depends on an earlier relocation's
// write as another REL addend.
//
// Planning is bounded by max_relocations, is read-only, and returns only
// logical 32-bit guest addresses/values; no host pointer is exposed.
[[nodiscard]] Elf32RelocationPlanResult build_elf32_rel_relocation_plan(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Decode and validate one loaded object's feature-008 PLT REL table without
// mutating guest memory. This path accepts only R_ARM_JUMP_SLOT. Each entry
// requires a word-aligned readable target and captures the original word only
// as rollback state; eager JUMP_SLOT semantics do not use it as an addend.
// Planning is independently bounded by max_relocations and returns only
// logical 32-bit guest addresses/values.
[[nodiscard]] Elf32RelocationPlanResult build_elf32_plt_rel_relocation_plan(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Resolve symbol-bearing ABS32/REL32/GLOB_DAT references without writing guest
// memory. Only GLOBAL/WEAK DEFAULT NOTYPE/OBJECT/FUNC references are accepted.
// Protected/TLS/IFUNC/common/XINDEX semantics fail explicitly. Versioned
// references are matched through bounded DT_VERSYM/VERNEED/VERDEF metadata.
// Plain lookup remains graph-local; callers may additionally provide an ordered
// in-graph global scope, and DT_SYMBOLIC/DF_SYMBOLIC requesters bind themselves
// before that scope. A lookup miss becomes S=0 only for a WEAK reference.
[[nodiscard]] Elf32RelocationResolutionResult
resolve_elf32_rel_relocation_references(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Resolve PLT R_ARM_JUMP_SLOT references without writing guest memory. The
// same bounded GLOBAL/WEAK DEFAULT NOTYPE/OBJECT/FUNC reference policy used by
// main REL relocations applies here, including caller-provided global scope and
// DT_SYMBOLIC/DF_SYMBOLIC requester-first ordering. A lookup miss becomes S=0
// only for a WEAK reference. Lazy binding and DT_PLTGOT resolver state are not involved.
[[nodiscard]] Elf32RelocationResolutionResult
resolve_elf32_plt_rel_relocation_references(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Apply the supported main-DT_REL relocations transactionally.
//
// Before the first write this function completes plan construction, reference
// resolution, RELATIVE symbol-index validation, and every final-word
// calculation. Formulas are 32-bit modulo arithmetic:
//   RELATIVE = B + A
//   GLOB_DAT = S        (Android/bionic ARM behavior: REL addend ignored)
//   ABS32    = S + A
//   REL32    = ((S + A) | T) - P
// R_ARM_NONE produces no write.
//
// Writes use GuestMemory only and occur in REL table order. On a later write
// failure, earlier successful writes are restored in reverse order from their
// captured original words. Restoration is verified by rereading each word.
// Rollback failure is reported distinctly while preserving TargetWriteFailed
// as primary_error. The function never changes guest page permissions. On any
// failure application.writes is empty; when RollbackFailed is returned guest
// state may remain partially relocated and rollback_failing_relocation
// identifies the first restoration that could not be established.
[[nodiscard]] Elf32RelocationApplyResult apply_elf32_rel_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Eagerly apply the feature-009 PLT R_ARM_JUMP_SLOT table transactionally.
// Every reference is resolved and every final word is computed before the
// first write. JUMP_SLOT stores S exactly; the in-place REL word is retained
// only for rollback and is never treated as an addend. Unresolved WEAK
// references therefore write zero. Writes occur in PLT-table order and reuse
// the same verified reverse-rollback contract as main REL application. This
// function does not implement lazy binding, DT_PLTGOT resolver state, guest
// execution, or page-permission changes.
[[nodiscard]] Elf32RelocationApplyResult apply_elf32_plt_rel_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

// Atomically prepare and apply the supported main REL and eager PLT REL
// tables for one object. Both tables complete semantic validation before the
// first write. Writes run main then PLT and roll back as one reverse-ordered
// transaction. Cross-table duplicate targets fail before mutation.
[[nodiscard]] Elf32RelocationApplyResult apply_elf32_combined_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options);

[[nodiscard]] const char* to_string(Elf32RelocationPlanError error) noexcept;
[[nodiscard]] const char* to_string(Elf32RelocationResolveError error) noexcept;
[[nodiscard]] const char* to_string(Elf32RelocationApplyError error) noexcept;

}  // namespace liba32android::elf
