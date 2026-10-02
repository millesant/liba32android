#include "elf/elf32_relocation.h"

#include "elf/internal/elf32_bytes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_set>

namespace liba32android::elf {
namespace {

constexpr std::uint32_t kElf32RelEntrySize = 8;
constexpr std::size_t kRelocationReadChunkBytes = 256;
constexpr std::uint32_t kRelocationsPerReadChunk =
    static_cast<std::uint32_t>(
        kRelocationReadChunkBytes / kElf32RelEntrySize);

static_assert((kRelocationReadChunkBytes % kElf32RelEntrySize) == 0);

constexpr std::uint8_t kStbGlobal = 1;
constexpr std::uint8_t kStbWeak = 2;
constexpr std::uint8_t kSttNotype = 0;
constexpr std::uint8_t kSttObject = 1;
constexpr std::uint8_t kSttFunc = 2;
constexpr std::uint8_t kStvDefault = 0;
constexpr std::uint16_t kShnLoReserve = 0xff00;
constexpr std::uint16_t kShnAbs = 0xfff1;
constexpr std::uint16_t kShnCommon = 0xfff2;
constexpr std::uint16_t kShnXindex = 0xffff;

[[nodiscard]] Elf32RelocationPlanResult failure(
    Elf32RelocationPlanError error,
    std::size_t object_index) {
    Elf32RelocationPlanResult result;
    result.error = error;
    result.plan.object_index = object_index;
    return result;
}

[[nodiscard]] bool read_word(const memory::GuestMemory& memory,
                             std::uint32_t address,
                             std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = detail::decode_u32_le(bytes.data());
    return true;
}

[[nodiscard]] bool supported_type(std::uint8_t type) noexcept {
    return type == kRArmNone ||
           type == kRArmAbs32 ||
           type == kRArmRel32 ||
           type == kRArmGlobDat ||
           type == kRArmRelative;
}

[[nodiscard]] bool symbol_bearing_type(std::uint8_t type) noexcept {
    return type == kRArmAbs32 ||
           type == kRArmRel32 ||
           type == kRArmGlobDat ||
           type == kRArmJumpSlot;
}

[[nodiscard]] bool valid_symbol_options(
    const Elf32SymbolLookupOptions& options) noexcept {
    return options.max_symbols != 0 &&
           options.max_hash_buckets != 0 &&
           options.max_gnu_bloom_words != 0 &&
           options.max_scope_objects != 0 &&
           options.max_name_bytes != 0;
}

[[nodiscard]] Elf32RelocationResolutionResult resolve_failure(
    Elf32RelocationResolveError error,
    std::size_t object_index,
    std::optional<std::uint32_t> failing_relocation = std::nullopt) {
    Elf32RelocationResolutionResult result;
    result.error = error;
    result.resolution.object_index = object_index;
    result.failing_relocation = failing_relocation;
    return result;
}

[[nodiscard]] std::uint32_t wrap_add(std::uint32_t lhs,
                                     std::uint32_t rhs) noexcept {
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(lhs) + rhs);
}

[[nodiscard]] std::uint32_t wrap_sub(std::uint32_t lhs,
                                     std::uint32_t rhs) noexcept {
    return static_cast<std::uint32_t>(lhs - rhs);
}

[[nodiscard]] bool write_word(memory::GuestMemory& memory,
                              std::uint32_t address,
                              std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
    return memory.write(address, bytes);
}

[[nodiscard]] bool restore_word(memory::GuestMemory& memory,
                                std::uint32_t address,
                                std::uint32_t original) {
    if (!write_word(memory, address, original)) return false;
    std::uint32_t verify = 0;
    return read_word(memory, address, verify) && verify == original;
}

struct PendingRelocationWrite {
    Elf32RelocationWrite write;
};

[[nodiscard]] Elf32RelocationApplyResult apply_failure(
    Elf32RelocationApplyError error,
    Elf32RelocationApplyError primary,
    std::size_t object_index,
    std::optional<std::uint32_t> failing_relocation = std::nullopt,
    std::optional<Elf32RelocationTable> failing_table = std::nullopt) {
    Elf32RelocationApplyResult result;
    result.error = error;
    result.primary_error = primary;
    result.application.object_index = object_index;
    result.failing_relocation = failing_relocation;
    result.failing_table = failing_table;
    return result;
}

}  // namespace

namespace {

[[nodiscard]] bool supported_type_for_table(
    std::uint8_t type,
    Elf32RelocationTable kind) noexcept {
    if (kind == Elf32RelocationTable::PltRel) {
        return type == kRArmJumpSlot;
    }
    return supported_type(type);
}

[[nodiscard]] Elf32RelocationPlanResult build_relocation_plan_for_table(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options,
    Elf32RelocationTable kind) {
    if (object_index >= graph.objects.size()) {
        return failure(Elf32RelocationPlanError::InvalidGraphObject,
                       object_index);
    }

    Elf32RelocationPlanResult result;
    result.plan.object_index = object_index;

    const Elf32LoadedDependencyObject& object = graph.objects[object_index];
    const std::optional<Elf32RelTableMetadata>& selected_table =
        kind == Elf32RelocationTable::PltRel
            ? object.linker_metadata.plt_rel_table
            : object.linker_metadata.rel_table;
    if (!selected_table.has_value()) {
        return result;
    }

    if (options.max_relocations == 0) {
        return failure(Elf32RelocationPlanError::InvalidOptions,
                       object_index);
    }

    const Elf32RelTableMetadata& table = *selected_table;
    // Validated linker metadata guarantees these invariants. Keep a
    // defensive check because callers can construct the public metadata type
    // directly in tests/embedders.
    if (table.entry_size != kElf32RelEntrySize ||
        table.size % kElf32RelEntrySize != 0) {
        return failure(Elf32RelocationPlanError::RelocationReadFailed,
                       object_index);
    }

    const std::uint32_t count = table.size / kElf32RelEntrySize;
    if (count > options.max_relocations) {
        return failure(Elf32RelocationPlanError::TooManyRelocations,
                       object_index);
    }

    result.plan.entries.reserve(count);
    std::unordered_set<std::uint32_t> write_targets;
    write_targets.reserve(count);

    std::array<std::uint8_t, kRelocationReadChunkBytes> chunk{};
    for (std::uint32_t chunk_start = 0; chunk_start < count;) {
        const std::uint32_t entries_in_chunk =
            std::min<std::uint32_t>(
                count - chunk_start, kRelocationsPerReadChunk);
        const std::size_t chunk_size =
            static_cast<std::size_t>(entries_in_chunk) *
            kElf32RelEntrySize;

        std::uint32_t chunk_address = 0;
        if (!detail::checked_add_guest_address(
                table.guest_address,
                static_cast<std::uint64_t>(chunk_start) *
                    kElf32RelEntrySize,
                chunk_address)) {
            return failure(Elf32RelocationPlanError::RelocationReadFailed,
                           object_index);
        }

        const std::uint64_t chunk_end =
            static_cast<std::uint64_t>(chunk_address) + chunk_size;
        const bool batch_read =
            chunk_end <= (std::uint64_t{1} << 32) &&
            memory.read(
                chunk_address,
                std::span<std::uint8_t>{chunk}.first(chunk_size));

        for (std::uint32_t local = 0; local < entries_in_chunk; ++local) {
            const std::uint32_t i = chunk_start + local;
            std::array<std::uint8_t, kElf32RelEntrySize> single{};
            const std::uint8_t* bytes = nullptr;

            if (batch_read) {
                bytes =
                    chunk.data() +
                    static_cast<std::size_t>(local) *
                        kElf32RelEntrySize;
            } else {
                std::uint32_t entry_address = 0;
                if (!detail::checked_add_guest_address(
                        table.guest_address,
                        static_cast<std::uint64_t>(i) *
                            kElf32RelEntrySize,
                        entry_address) ||
                    !memory.read(entry_address, single)) {
                    return failure(
                        Elf32RelocationPlanError::RelocationReadFailed,
                        object_index);
                }
                bytes = single.data();
            }

            Elf32RelocationEntry entry;
            entry.index = i;
            entry.offset = detail::decode_u32_le(bytes);
            entry.info = detail::decode_u32_le(bytes + 4);
            entry.symbol_index = entry.info >> 8U;
            entry.type =
                static_cast<std::uint8_t>(entry.info & 0xffU);

            if (!supported_type_for_table(entry.type, kind)) {
                return failure(
                    Elf32RelocationPlanError::UnsupportedRelocationType,
                    object_index);
            }

            if (!detail::checked_add_guest_address(
                    object.load.load_bias,
                    entry.offset,
                    entry.place_guest_address)) {
                return failure(Elf32RelocationPlanError::PlaceOverflow,
                               object_index);
            }

            if (entry.type != kRArmNone) {
                if ((entry.place_guest_address & 0x3U) != 0) {
                    return failure(
                        Elf32RelocationPlanError::UnalignedPlace,
                        object_index);
                }

                std::uint32_t original = 0;
                if (!read_word(
                        memory, entry.place_guest_address, original)) {
                    return failure(
                        Elf32RelocationPlanError::TargetReadFailed,
                        object_index);
                }
                entry.original_word = original;

                if (!write_targets
                         .insert(entry.place_guest_address)
                         .second) {
                    return failure(
                        Elf32RelocationPlanError::DuplicateTarget,
                        object_index);
                }
            }

            result.plan.entries.push_back(entry);
        }

        chunk_start += entries_in_chunk;
    }

    return result;
}

[[nodiscard]] Elf32RelocationResolutionResult
resolve_relocation_references_for_table(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options,
    Elf32RelocationTable kind) {
    const Elf32RelocationPlanResult plan =
        build_relocation_plan_for_table(
            memory, graph, object_index, options, kind);
    if (!plan) {
        auto result = resolve_failure(
            Elf32RelocationResolveError::PlanFailed, object_index);
        result.plan_error = plan.error;
        return result;
    }

    Elf32RelocationResolutionResult result;
    result.resolution.object_index = object_index;
    result.resolution.entries.reserve(plan.plan.entries.size());

    bool needs_symbols = false;
    for (const Elf32RelocationEntry& entry : plan.plan.entries) {
        if (symbol_bearing_type(entry.type)) {
            needs_symbols = true;
            break;
        }
    }
    if (!needs_symbols) {
        for (const Elf32RelocationEntry& entry : plan.plan.entries) {
            result.resolution.entries.push_back(
                Elf32ResolvedRelocationEntry{.relocation = entry});
        }
        return result;
    }
    if (object_index >= graph.objects.size() ||
        !valid_symbol_options(options.symbols)) {
        return resolve_failure(
            Elf32RelocationResolveError::InvalidOptions, object_index);
    }

    const Elf32LoadedDependencyObject& object = graph.objects[object_index];

    const Elf32SymbolIndexResult index =
        build_elf32_symbol_index(memory, object.linker_metadata,
                                 options.symbols);
    if (!index) {
        auto failed = resolve_failure(
            Elf32RelocationResolveError::IndexBuildFailed, object_index);
        failed.index_error = index.error;
        return failed;
    }

    for (const Elf32RelocationEntry& entry : plan.plan.entries) {
        Elf32ResolvedRelocationEntry resolved;
        resolved.relocation = entry;
        if (!symbol_bearing_type(entry.type)) {
            result.resolution.entries.push_back(std::move(resolved));
            continue;
        }
        if (entry.symbol_index == 0) {
            return resolve_failure(
                Elf32RelocationResolveError::MissingReferenceSymbol,
                object_index, entry.index);
        }
        if (entry.symbol_index >= index.index.symbol_count) {
            return resolve_failure(
                Elf32RelocationResolveError::SymbolIndexOutOfRange,
                object_index, entry.index);
        }

        const Elf32SymbolReadResult symbol_result =
            read_elf32_symbol_entry(memory, object.linker_metadata,
                                    index.index, entry.symbol_index);
        if (!symbol_result) {
            auto failed = resolve_failure(
                Elf32RelocationResolveError::ReferenceSymbolReadFailed,
                object_index, entry.index);
            failed.symbol_read_error = symbol_result.error;
            return failed;
        }
        const Elf32Symbol& symbol = symbol_result.symbol;

        if (!object.linker_metadata.string_table.has_value()) {
            auto failed = resolve_failure(
                Elf32RelocationResolveError::ReferenceNameFailed,
                object_index, entry.index);
            failed.string_error = Elf32LinkerStringError::MissingStringTable;
            return failed;
        }
        const Elf32SingleStringResult name =
            read_elf32_string_table_entry(
                memory, *object.linker_metadata.string_table,
                symbol.name_offset,
                Elf32LinkerStringOptions{
                    .max_string_bytes = options.symbols.max_name_bytes,
                });
        if (!name || name.value.empty()) {
            auto failed = resolve_failure(
                Elf32RelocationResolveError::ReferenceNameFailed,
                object_index, entry.index);
            failed.string_error = name.error;
            return failed;
        }

        if (symbol.binding != kStbGlobal && symbol.binding != kStbWeak) {
            return resolve_failure(
                Elf32RelocationResolveError::UnsupportedReferenceBinding,
                object_index, entry.index);
        }
        if ((symbol.raw_other & 0xfcU) != 0 ||
            symbol.visibility != kStvDefault) {
            return resolve_failure(
                Elf32RelocationResolveError::UnsupportedReferenceVisibility,
                object_index, entry.index);
        }
        if (symbol.type != kSttNotype &&
            symbol.type != kSttObject &&
            symbol.type != kSttFunc) {
            return resolve_failure(
                Elf32RelocationResolveError::UnsupportedReferenceType,
                object_index, entry.index);
        }
        if (symbol.section_index == kShnCommon ||
            symbol.section_index == kShnXindex ||
            (symbol.section_index >= kShnLoReserve &&
             symbol.section_index != kShnAbs)) {
            return resolve_failure(
                Elf32RelocationResolveError::UnsupportedReferenceSection,
                object_index, entry.index);
        }

        Elf32RelocationReference reference;
        reference.symbol_index = entry.symbol_index;
        reference.name = name.value;
        reference.symbol = symbol;

        const Elf32GraphSymbolLookupResult lookup =
            lookup_elf32_graph_symbol_for_reference(
                memory, graph, object_index, entry.symbol_index,
                name.value, options.symbols);
        if (lookup) {
            reference.defining_symbol = lookup.symbol.symbol.symbol;
            reference.symbol_value = lookup.symbol.symbol.guest_value;
            reference.defining_object_index = lookup.symbol.object_index;
            reference.defining_symbol_index =
                lookup.symbol.symbol.symbol_index;
        } else if (lookup.error ==
                   Elf32GraphSymbolLookupError::SymbolNotFound) {
            if (symbol.binding != kStbWeak) {
                return resolve_failure(
                    Elf32RelocationResolveError::UnresolvedStrongSymbol,
                    object_index, entry.index);
            }
            reference.symbol_value = 0;
            reference.unresolved_weak = true;
        } else {
            auto failed = resolve_failure(
                Elf32RelocationResolveError::SymbolLookupFailed,
                object_index, entry.index);
            failed.graph_error = lookup.error;
            failed.index_error = lookup.index_error;
            failed.lookup_error = lookup.lookup_error;
            failed.string_error = lookup.string_error;
            failed.failing_object = lookup.failing_object;
            return failed;
        }

        resolved.reference = std::move(reference);
        result.resolution.entries.push_back(std::move(resolved));
    }
    return result;
}

}  // namespace

Elf32RelocationPlanResult build_elf32_rel_relocation_plan(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return build_relocation_plan_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::MainRel);
}

Elf32RelocationPlanResult build_elf32_plt_rel_relocation_plan(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return build_relocation_plan_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::PltRel);
}

Elf32RelocationResolutionResult resolve_elf32_rel_relocation_references(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return resolve_relocation_references_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::MainRel);
}

Elf32RelocationResolutionResult
resolve_elf32_plt_rel_relocation_references(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return resolve_relocation_references_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::PltRel);
}

namespace {

[[nodiscard]] Elf32RelocationApplyResult prepare_relocations_for_table(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options,
    Elf32RelocationTable kind,
    std::vector<PendingRelocationWrite>& pending) {
    Elf32RelocationResolutionResult resolution =
        resolve_relocation_references_for_table(
            memory, graph, object_index, options, kind);
    if (!resolution) {
        auto result = apply_failure(
            Elf32RelocationApplyError::ResolveFailed,
            Elf32RelocationApplyError::ResolveFailed,
            object_index, resolution.failing_relocation, kind);
        result.resolution_failure = std::move(resolution);
        return result;
    }
    if (object_index >= graph.objects.size()) {
        return apply_failure(
            Elf32RelocationApplyError::InvalidResolvedEntry,
            Elf32RelocationApplyError::InvalidResolvedEntry,
            object_index, std::nullopt, kind);
    }

    const std::uint32_t load_bias = graph.objects[object_index].load.load_bias;
    pending.clear();
    pending.reserve(resolution.resolution.entries.size());
    for (const Elf32ResolvedRelocationEntry& resolved : resolution.resolution.entries) {
        const Elf32RelocationEntry& entry = resolved.relocation;
        if (entry.type == kRArmNone) continue;
        if (!entry.original_word.has_value()) {
            return apply_failure(
                Elf32RelocationApplyError::InvalidResolvedEntry,
                Elf32RelocationApplyError::InvalidResolvedEntry,
                object_index, entry.index, kind);
        }

        std::uint32_t final_word = 0;
        switch (entry.type) {
        case kRArmRelative:
            if (entry.symbol_index != 0) {
                return apply_failure(
                    Elf32RelocationApplyError::InvalidRelativeSymbol,
                    Elf32RelocationApplyError::InvalidRelativeSymbol,
                    object_index, entry.index, kind);
            }
            final_word = wrap_add(load_bias, *entry.original_word);
            break;
        case kRArmGlobDat:
        case kRArmJumpSlot:
            if (!resolved.reference.has_value()) {
                return apply_failure(
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    object_index, entry.index, kind);
            }
            final_word = resolved.reference->symbol_value;
            break;
        case kRArmAbs32:
            if (!resolved.reference.has_value()) {
                return apply_failure(
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    object_index, entry.index, kind);
            }
            final_word = wrap_add(resolved.reference->symbol_value, *entry.original_word);
            break;
        case kRArmRel32: {
            if (!resolved.reference.has_value()) {
                return apply_failure(
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    Elf32RelocationApplyError::InvalidResolvedEntry,
                    object_index, entry.index, kind);
            }
            const Elf32RelocationReference& reference = *resolved.reference;
            std::uint32_t relocation_symbol_value = reference.symbol_value;
            std::uint32_t thumb_bit = 0;
            if (!reference.unresolved_weak &&
                reference.defining_symbol.has_value() &&
                reference.defining_symbol->type == kSttFunc &&
                (reference.defining_symbol->value & 1U) != 0) {
                // AAELF32 defines S for relocation with the Thumb discriminator
                // stripped, and reintroduces it separately as T after S + A.
                relocation_symbol_value &= ~1U;
                thumb_bit = 1;
            }
            final_word = wrap_sub(
                wrap_add(relocation_symbol_value, *entry.original_word) |
                    thumb_bit,
                entry.place_guest_address);
            break;
        }
        default:
            return apply_failure(
                Elf32RelocationApplyError::InvalidResolvedEntry,
                Elf32RelocationApplyError::InvalidResolvedEntry,
                object_index, entry.index, kind);
        }

        pending.push_back(PendingRelocationWrite{.write = Elf32RelocationWrite{
            .relocation_index = entry.index,
            .type = entry.type,
            .place_guest_address = entry.place_guest_address,
            .original_word = *entry.original_word,
            .final_word = final_word,
            .table = kind,
        }});
    }

    Elf32RelocationApplyResult result;
    result.application.object_index = object_index;
    return result;
}

[[nodiscard]] Elf32RelocationApplyResult apply_pending_relocations(
    memory::GuestMemory& memory,
    std::size_t object_index,
    const std::vector<PendingRelocationWrite>& pending) {
    std::vector<std::size_t> applied;
    applied.reserve(pending.size());
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const Elf32RelocationWrite& write = pending[i].write;
        if (write_word(memory, write.place_guest_address, write.final_word)) {
            applied.push_back(i);
            continue;
        }

        bool rollback_failed = false;
        std::optional<std::uint32_t> rollback_failure;
        std::optional<Elf32RelocationTable> rollback_failure_table;
        std::uint32_t failed_target_now = 0;
        const bool failed_target_unchanged =
            read_word(memory, write.place_guest_address, failed_target_now) &&
            failed_target_now == write.original_word;
        if (!failed_target_unchanged &&
            !restore_word(memory, write.place_guest_address, write.original_word)) {
            rollback_failed = true;
            rollback_failure = write.relocation_index;
            rollback_failure_table = write.table;
        }
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            const Elf32RelocationWrite& previous = pending[*it].write;
            if (!restore_word(memory, previous.place_guest_address, previous.original_word)) {
                if (!rollback_failure.has_value()) {
                    rollback_failure = previous.relocation_index;
                    rollback_failure_table = previous.table;
                }
                rollback_failed = true;
            }
        }

        auto result = apply_failure(
            rollback_failed ? Elf32RelocationApplyError::RollbackFailed
                            : Elf32RelocationApplyError::TargetWriteFailed,
            Elf32RelocationApplyError::TargetWriteFailed,
            object_index, write.relocation_index, write.table);
        result.rollback_failing_relocation = rollback_failure;
        result.rollback_failing_table = rollback_failure_table;
        return result;
    }

    Elf32RelocationApplyResult result;
    result.application.object_index = object_index;
    result.application.writes.reserve(pending.size());
    for (const PendingRelocationWrite& item : pending) {
        result.application.writes.push_back(item.write);
    }
    return result;
}

[[nodiscard]] Elf32RelocationApplyResult apply_relocations_for_table(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options,
    Elf32RelocationTable kind) {
    std::vector<PendingRelocationWrite> pending;
    Elf32RelocationApplyResult prepared = prepare_relocations_for_table(
        memory, graph, object_index, options, kind, pending);
    if (!prepared) return prepared;
    return apply_pending_relocations(memory, object_index, pending);
}

}  // namespace

Elf32RelocationApplyResult apply_elf32_rel_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return apply_relocations_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::MainRel);
}

Elf32RelocationApplyResult apply_elf32_plt_rel_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    return apply_relocations_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::PltRel);
}

Elf32RelocationApplyResult apply_elf32_combined_relocations(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t object_index,
    const Elf32RelocationOptions& options) {
    std::vector<PendingRelocationWrite> main_pending;
    Elf32RelocationApplyResult main_prepared = prepare_relocations_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::MainRel,
        main_pending);
    if (!main_prepared) return main_prepared;

    std::vector<PendingRelocationWrite> plt_pending;
    Elf32RelocationApplyResult plt_prepared = prepare_relocations_for_table(
        memory, graph, object_index, options, Elf32RelocationTable::PltRel,
        plt_pending);
    if (!plt_prepared) return plt_prepared;

    std::unordered_set<std::uint32_t> targets;
    targets.reserve(main_pending.size() + plt_pending.size());
    for (const PendingRelocationWrite& item : main_pending) {
        targets.insert(item.write.place_guest_address);
    }
    for (const PendingRelocationWrite& item : plt_pending) {
        if (!targets.insert(item.write.place_guest_address).second) {
            return apply_failure(
                Elf32RelocationApplyError::DuplicateTargetAcrossTables,
                Elf32RelocationApplyError::DuplicateTargetAcrossTables,
                object_index, item.write.relocation_index,
                Elf32RelocationTable::PltRel);
        }
    }

    std::vector<PendingRelocationWrite> combined;
    combined.reserve(main_pending.size() + plt_pending.size());
    combined.insert(combined.end(), main_pending.begin(), main_pending.end());
    combined.insert(combined.end(), plt_pending.begin(), plt_pending.end());
    return apply_pending_relocations(memory, object_index, combined);
}

const char* to_string(Elf32RelocationPlanError error) noexcept {
    switch (error) {
    case Elf32RelocationPlanError::None: return "none";
    case Elf32RelocationPlanError::InvalidOptions: return "invalid_options";
    case Elf32RelocationPlanError::InvalidGraphObject: return "invalid_graph_object";
    case Elf32RelocationPlanError::TooManyRelocations: return "too_many_relocations";
    case Elf32RelocationPlanError::RelocationReadFailed: return "relocation_read_failed";
    case Elf32RelocationPlanError::PlaceOverflow: return "place_overflow";
    case Elf32RelocationPlanError::UnalignedPlace: return "unaligned_place";
    case Elf32RelocationPlanError::TargetReadFailed: return "target_read_failed";
    case Elf32RelocationPlanError::DuplicateTarget: return "duplicate_target";
    case Elf32RelocationPlanError::UnsupportedRelocationType:
        return "unsupported_relocation_type";
    }
    return "unknown";
}

const char* to_string(Elf32RelocationResolveError error) noexcept {
    switch (error) {
    case Elf32RelocationResolveError::None: return "none";
    case Elf32RelocationResolveError::InvalidOptions: return "invalid_options";
    case Elf32RelocationResolveError::PlanFailed: return "plan_failed";
    case Elf32RelocationResolveError::IndexBuildFailed: return "index_build_failed";
    case Elf32RelocationResolveError::MissingReferenceSymbol: return "missing_reference_symbol";
    case Elf32RelocationResolveError::SymbolIndexOutOfRange: return "symbol_index_out_of_range";
    case Elf32RelocationResolveError::ReferenceSymbolReadFailed: return "reference_symbol_read_failed";
    case Elf32RelocationResolveError::ReferenceNameFailed: return "reference_name_failed";
    case Elf32RelocationResolveError::UnsupportedVersioning: return "unsupported_versioning";
    case Elf32RelocationResolveError::UnsupportedReferenceBinding: return "unsupported_reference_binding";
    case Elf32RelocationResolveError::UnsupportedReferenceVisibility: return "unsupported_reference_visibility";
    case Elf32RelocationResolveError::UnsupportedReferenceType: return "unsupported_reference_type";
    case Elf32RelocationResolveError::UnsupportedReferenceSection: return "unsupported_reference_section";
    case Elf32RelocationResolveError::SymbolLookupFailed: return "symbol_lookup_failed";
    case Elf32RelocationResolveError::UnresolvedStrongSymbol: return "unresolved_strong_symbol";
    }
    return "unknown";
}

const char* to_string(Elf32RelocationApplyError error) noexcept {
    switch (error) {
    case Elf32RelocationApplyError::None: return "none";
    case Elf32RelocationApplyError::ResolveFailed: return "resolve_failed";
    case Elf32RelocationApplyError::InvalidRelativeSymbol:
        return "invalid_relative_symbol";
    case Elf32RelocationApplyError::InvalidResolvedEntry:
        return "invalid_resolved_entry";
    case Elf32RelocationApplyError::DuplicateTargetAcrossTables:
        return "duplicate_target_across_tables";
    case Elf32RelocationApplyError::TargetWriteFailed:
        return "target_write_failed";
    case Elf32RelocationApplyError::RollbackFailed:
        return "rollback_failed";
    }
    return "unknown";
}

}  // namespace liba32android::elf
