#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "elf/elf32_relocation.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32HashTableMetadata;
using liba32android::elf::Elf32LinkerStringError;
using liba32android::elf::Elf32RelTableMetadata;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32RelocationPlanError;
using liba32android::elf::Elf32RelocationResolveError;
using liba32android::elf::Elf32StringTableMetadata;
using liba32android::elf::Elf32SymbolIndexError;
using liba32android::elf::Elf32SymbolTableMetadata;
using liba32android::elf::build_elf32_plt_rel_relocation_plan;
using liba32android::elf::build_elf32_rel_relocation_plan;
using liba32android::elf::resolve_elf32_plt_rel_relocation_references;
using liba32android::elf::resolve_elf32_rel_relocation_references;
using liba32android::elf::kRArmAbs32;
using liba32android::elf::kRArmGlobDat;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::kRArmNone;
using liba32android::elf::kRArmRelative;
using liba32android::memory::GuestMemory;
using liba32android::memory::LinearGuestMemory;

constexpr std::uint32_t kMemoryBase = 0x1000;
constexpr std::uint32_t kRelTable = 0x1100;
constexpr std::uint32_t kPltRelTable = 0x1180;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class CountingGuestMemory final : public GuestMemory {
public:
    CountingGuestMemory(std::size_t size, std::uint32_t base)
        : backing_{size, base},
          readable_end_{static_cast<std::uint64_t>(base) + size} {}

    bool read(
        std::uint32_t address,
        std::span<std::uint8_t> output) const override {
        ++read_calls_;
        const std::uint64_t end =
            static_cast<std::uint64_t>(address) + output.size();
        if (end > readable_end_) {
            return false;
        }
        return backing_.read(address, output);
    }

    bool write(
        std::uint32_t address,
        std::span<const std::uint8_t> input) override {
        return backing_.write(address, input);
    }

    [[nodiscard]] LinearGuestMemory& backing() noexcept {
        return backing_;
    }

    void reset_read_calls() noexcept {
        read_calls_ = 0;
    }

    void set_readable_end(std::uint64_t readable_end) noexcept {
        readable_end_ = readable_end;
    }

    [[nodiscard]] std::size_t read_calls() const noexcept {
        return read_calls_;
    }

private:
    LinearGuestMemory backing_;
    std::uint64_t readable_end_{};
    mutable std::size_t read_calls_{};
};

bool write_u32(LinearGuestMemory& memory,
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

bool read_u32(const LinearGuestMemory& memory,
              std::uint32_t address,
              std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

bool write_rel_at(LinearGuestMemory& memory,
                  std::uint32_t table,
                  std::uint32_t index,
                  std::uint32_t offset,
                  std::uint32_t symbol_index,
                  std::uint8_t type) {
    return write_u32(memory, table + index * 8U, offset) &&
           write_u32(memory, table + index * 8U + 4U,
                     (symbol_index << 8U) | type);
}

bool write_rel(LinearGuestMemory& memory,
               std::uint32_t index,
               std::uint32_t offset,
               std::uint32_t symbol_index,
               std::uint8_t type) {
    return write_rel_at(
        memory, kRelTable, index, offset, symbol_index, type);
}

Elf32DependencyGraph graph_with_rel(std::uint32_t load_bias,
                                    std::uint32_t table_address,
                                    std::uint32_t table_size) {
    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    graph.objects[0].identity = "relocation-test";
    graph.objects[0].load.load_bias = load_bias;
    graph.objects[0].linker_metadata.rel_table =
        Elf32RelTableMetadata{
            .guest_address = table_address,
            .size = table_size,
            .entry_size = 8,
        };
    return graph;
}

Elf32DependencyGraph graph_with_plt_rel(std::uint32_t load_bias,
                                        std::uint32_t table_address,
                                        std::uint32_t table_size) {
    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    graph.objects[0].identity = "plt-relocation-test";
    graph.objects[0].load.load_bias = load_bias;
    graph.objects[0].linker_metadata.plt_rel_table =
        Elf32RelTableMetadata{
            .guest_address = table_address,
            .size = table_size,
            .entry_size = 8,
        };
    return graph;
}

Elf32RelocationOptions options(std::uint32_t max_relocations = 16) {
    Elf32RelocationOptions result;
    result.max_relocations = max_relocations;
    result.symbols.max_symbols = 16;
    result.symbols.max_hash_buckets = 8;
    result.symbols.max_gnu_bloom_words = 8;
    result.symbols.max_scope_objects = 8;
    result.symbols.max_name_bytes = 64;
    return result;
}

bool write_string_at(LinearGuestMemory& memory,
                     std::uint32_t table,
                     std::uint32_t offset,
                     std::string_view value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    return memory.write(table + offset, bytes);
}

bool write_symbol_at(LinearGuestMemory& memory,
                     std::uint32_t table,
                     std::uint32_t index,
                     std::uint32_t name_offset,
                     std::uint32_t value,
                     std::uint8_t binding,
                     std::uint8_t type,
                     std::uint8_t other,
                     std::uint16_t section_index) {
    std::array<std::uint8_t, 16> bytes{};
    const auto put_u32 = [&](std::size_t offset, std::uint32_t word) {
        bytes[offset] = static_cast<std::uint8_t>(word);
        bytes[offset + 1] = static_cast<std::uint8_t>(word >> 8U);
        bytes[offset + 2] = static_cast<std::uint8_t>(word >> 16U);
        bytes[offset + 3] = static_cast<std::uint8_t>(word >> 24U);
    };
    put_u32(0, name_offset);
    put_u32(4, value);
    put_u32(8, 4);
    bytes[12] = static_cast<std::uint8_t>((binding << 4U) | (type & 0x0fU));
    bytes[13] = other;
    bytes[14] = static_cast<std::uint8_t>(section_index);
    bytes[15] = static_cast<std::uint8_t>(section_index >> 8U);
    return memory.write(table + index * 16U, bytes);
}

bool stage_symbol_object(LinearGuestMemory& memory,
                         liba32android::elf::Elf32LoadedDependencyObject& object,
                         std::size_t slot,
                         std::string_view name,
                         std::uint32_t load_bias,
                         std::uint8_t binding,
                         std::uint8_t type,
                         std::uint8_t other,
                         std::uint16_t section_index,
                         std::uint32_t value = 0,
                         std::uint32_t name_offset = 1) {
    const std::uint32_t base =
        0x4000U + static_cast<std::uint32_t>(slot) * 0x1000U;
    const std::uint32_t strings = base;
    const std::uint32_t symbols = base + 0x100U;
    const std::uint32_t hash = base + 0x300U;

    object.identity = "symbol-object-" + std::to_string(slot);
    object.load.load_bias = load_bias;
    object.linker_metadata.string_table =
        Elf32StringTableMetadata{.guest_address = strings, .size = 0x80};
    object.linker_metadata.symbol_table =
        Elf32SymbolTableMetadata{.guest_address = symbols, .entry_size = 16};
    object.linker_metadata.sysv_hash_table =
        Elf32HashTableMetadata{.guest_address = hash};

    if (name_offset < 0x80 &&
        !write_string_at(memory, strings, name_offset, name)) {
        return false;
    }
    return write_symbol_at(memory, symbols, 1, name_offset, value,
                           binding, type, other, section_index) &&
           write_u32(memory, hash, 1) &&
           write_u32(memory, hash + 4U, 2) &&
           write_u32(memory, hash + 8U, 1) &&
           write_u32(memory, hash + 12U, 0) &&
           write_u32(memory, hash + 16U, 0);
}

int test_empty_and_exact_decode() {
    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        const auto result =
            build_elf32_rel_relocation_plan(memory, graph, 0, options(0));
        if (!result || !result.plan.entries.empty()) {
            return fail("object without REL table did not produce empty success");
        }
    }

    LinearGuestMemory memory(0x5000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, kRelTable, 32);

    if (!write_rel(memory, 0, 0x7000, 9, kRArmNone) ||
        !write_rel(memory, 1, 0x1000, 3, kRArmAbs32) ||
        !write_rel(memory, 2, 0x1004, 2, kRArmGlobDat) ||
        !write_rel(memory, 3, 0x1008, 0, kRArmRelative) ||
        !write_u32(memory, 0x2000, 0x11223344U) ||
        !write_u32(memory, 0x2004, 0xaabbccddU) ||
        !write_u32(memory, 0x2008, 0x01020304U)) {
        return fail("could not stage valid REL plan");
    }

    const auto result =
        build_elf32_rel_relocation_plan(memory, graph, 0, options());
    if (!result || result.plan.object_index != 0 ||
        result.plan.entries.size() != 4) {
        return fail("valid REL table did not decode");
    }

    const auto& none = result.plan.entries[0];
    if (none.offset != 0x7000 || none.symbol_index != 9 ||
        none.type != kRArmNone ||
        none.place_guest_address != 0x8000 ||
        none.original_word.has_value()) {
        return fail("R_ARM_NONE plan metadata was incorrect or read its target");
    }

    const auto& abs = result.plan.entries[1];
    const auto& glob = result.plan.entries[2];
    const auto& relative = result.plan.entries[3];
    if (abs.place_guest_address != 0x2000 ||
        abs.symbol_index != 3 ||
        !abs.original_word.has_value() ||
        *abs.original_word != 0x11223344U ||
        glob.place_guest_address != 0x2004 ||
        glob.symbol_index != 2 ||
        !glob.original_word.has_value() ||
        *glob.original_word != 0xaabbccddU ||
        relative.place_guest_address != 0x2008 ||
        relative.symbol_index != 0 ||
        !relative.original_word.has_value() ||
        *relative.original_word != 0x01020304U) {
        return fail("supported REL entry metadata/addends were decoded incorrectly");
    }

    std::uint32_t value = 0;
    if (!read_u32(memory, 0x2000, value) || value != 0x11223344U ||
        !read_u32(memory, 0x2004, value) || value != 0xaabbccddU ||
        !read_u32(memory, 0x2008, value) || value != 0x01020304U) {
        return fail("read-only REL planning mutated guest target words");
    }

    return 0;
}

int test_relocation_table_reads_are_batched_and_preserve_error_order() {
    CountingGuestMemory memory{0x8000, kMemoryBase};
    constexpr std::uint32_t kEntryCount = 64U;
    auto graph = graph_with_rel(
        0x1000, kRelTable, kEntryCount * 8U);
    for (std::uint32_t index = 0; index < kEntryCount; ++index) {
        if (!write_rel_at(
                memory.backing(),
                kRelTable,
                index,
                0x7000U + index * 4U,
                index,
                kRArmNone)) {
            return fail("could not stage batched REL table");
        }
    }

    memory.reset_read_calls();
    const auto result =
        build_elf32_rel_relocation_plan(
            memory, graph, 0, options(kEntryCount));
    if (!result || result.plan.entries.size() != kEntryCount ||
        memory.read_calls() != 2U) {
        return fail("REL planning did not batch readable table entries");
    }

    CountingGuestMemory ordered{0x4000, kMemoryBase};
    auto ordered_graph = graph_with_rel(0x1000, kRelTable, 16U);
    if (!write_rel_at(
            ordered.backing(),
            kRelTable,
            0,
            0x1000U,
            1U,
            kRArmJumpSlot) ||
        !write_rel_at(
            ordered.backing(),
            kRelTable,
            1,
            0x1004U,
            0U,
            kRArmNone)) {
        return fail("could not stage REL error-order fixture");
    }
    ordered.set_readable_end(
        static_cast<std::uint64_t>(kRelTable) + 8U);
    ordered.reset_read_calls();
    const auto ordered_result =
        build_elf32_rel_relocation_plan(
            ordered, ordered_graph, 0, options());
    if (ordered_result.error !=
            Elf32RelocationPlanError::UnsupportedRelocationType ||
        ordered.read_calls() != 2U) {
        return fail("REL batch fallback changed first-error ordering");
    }
    return 0;
}

int test_limits_and_graph_inputs() {
    LinearGuestMemory memory(0x4000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, kRelTable, 16);
    if (!write_rel(memory, 0, 0x1000, 1, kRArmAbs32) ||
        !write_rel(memory, 1, 0x1004, 2, kRArmGlobDat) ||
        !write_u32(memory, 0x2000, 0) ||
        !write_u32(memory, 0x2004, 0)) {
        return fail("could not stage limit test");
    }

    if (build_elf32_rel_relocation_plan(memory, graph, 0, options(0)).error !=
        Elf32RelocationPlanError::InvalidOptions) {
        return fail("zero relocation limit with REL table was not rejected");
    }
    if (build_elf32_rel_relocation_plan(memory, graph, 0, options(1)).error !=
        Elf32RelocationPlanError::TooManyRelocations) {
        return fail("relocation count ceiling was not enforced");
    }
    if (build_elf32_rel_relocation_plan(memory, graph, 1, options()).error !=
        Elf32RelocationPlanError::InvalidGraphObject) {
        return fail("invalid graph object index was not rejected");
    }

    graph.objects[0].linker_metadata.rel_table->entry_size = 4;
    if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
        Elf32RelocationPlanError::RelocationReadFailed) {
        return fail("crafted invalid REL entry size was not rejected defensively");
    }
    return 0;
}

int test_read_place_and_type_failures() {
    {
        LinearGuestMemory memory(0x1000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, 0x1ffc, 8);
        if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::RelocationReadFailed) {
            return fail("unreadable REL entry was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph = graph_with_rel(0xfffffff0U, kRelTable, 8);
        if (!write_rel(memory, 0, 0x20, 1, kRArmAbs32)) {
            return fail("could not stage place-overflow test");
        }
        if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::PlaceOverflow) {
            return fail("REL place overflow was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!write_rel(memory, 0, 0x1001, 1, kRArmAbs32)) {
            return fail("could not stage alignment test");
        }
        if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::UnalignedPlace) {
            return fail("unaligned supported relocation place was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x2000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!write_rel(memory, 0, 0x3000, 1, kRArmGlobDat)) {
            return fail("could not stage target-read test");
        }
        if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::TargetReadFailed) {
            return fail("unreadable supported relocation target was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x2000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!write_rel(memory, 0, 0x3000, 1, kRArmJumpSlot)) {
            return fail("could not stage unsupported-type test");
        }
        if (build_elf32_rel_relocation_plan(memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::UnsupportedRelocationType) {
            return fail("unsupported relocation type was not rejected before target access");
        }
    }

    return 0;
}

int test_reference_resolution_and_weak_behavior() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        graph.objects.resize(2);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !stage_symbol_object(memory, graph.objects[1], 1, "target",
                                 0x5000, 1, 1, 0, 1, 0x120) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0xdeadbeefU)) {
            return fail("could not stage graph-local relocation reference");
        }
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
        };

        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (!result || result.resolution.entries.size() != 1 ||
            !result.resolution.entries[0].reference.has_value()) {
            return fail("valid relocation reference did not resolve");
        }
        const auto& reference = *result.resolution.entries[0].reference;
        if (reference.name != "target" ||
            reference.symbol_index != 1 ||
            reference.symbol.binding != 1 ||
            reference.symbol.section_index != 0 ||
            reference.symbol_value != 0x5120 ||
            reference.unresolved_weak ||
            !reference.defining_object_index.has_value() ||
            *reference.defining_object_index != 1 ||
            !reference.defining_symbol_index.has_value() ||
            *reference.defining_symbol_index != 1) {
            return fail("graph-local relocation reference result was incorrect");
        }
        std::uint32_t target = 0;
        if (!read_u32(memory, 0x12000, target) ||
            target != 0xdeadbeefU) {
            return fail("reference resolution mutated guest target memory");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "missing",
                                 0x1000, 2, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmAbs32) ||
            !write_u32(memory, 0x12000, 0x12345678U)) {
            return fail("could not stage unresolved weak relocation reference");
        }
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (!result || result.resolution.entries.size() != 1 ||
            !result.resolution.entries[0].reference.has_value() ||
            !result.resolution.entries[0].reference->unresolved_weak ||
            result.resolution.entries[0].reference->symbol_value != 0 ||
            result.resolution.entries[0].reference->defining_object_index.has_value()) {
            return fail("unresolved weak relocation reference did not become S=0");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "missing",
                                 0x1000, 1, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage unresolved strong relocation reference");
        }
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error !=
            Elf32RelocationResolveError::UnresolvedStrongSymbol ||
            !result.failing_relocation.has_value() ||
            *result.failing_relocation != 0) {
            return fail("unresolved strong relocation reference was not rejected");
        }
    }
    return 0;
}

int test_reference_scope_policy_flows_through_relocation() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, kRelTable, 8);
    graph.objects.resize(3);

    if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                             0x1000, 1, 1, 0, 0) ||
        !stage_symbol_object(memory, graph.objects[1], 1, "target",
                             0x5000, 1, 1, 0, 1, 0x120) ||
        !stage_symbol_object(memory, graph.objects[2], 2, "target",
                             0x7000, 1, 1, 0, 1, 0x140) ||
        !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
        !write_u32(memory, 0x12000, 0)) {
        return fail("could not stage relocation scope-policy fixture");
    }

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "local", .target_object = 2},
    };
    const std::array<std::size_t, 1> global_scope{1};
    auto scoped = options();
    scoped.symbols.global_scope_objects = global_scope;

    const auto ordinary = resolve_elf32_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!ordinary || ordinary.resolution.entries.size() != 1 ||
        !ordinary.resolution.entries[0].reference.has_value()) {
        return fail("global-scope relocation reference did not resolve");
    }
    const auto& ordinary_reference =
        *ordinary.resolution.entries[0].reference;
    if (!ordinary_reference.defining_object_index.has_value() ||
        *ordinary_reference.defining_object_index != 1 ||
        ordinary_reference.symbol_value != 0x5120) {
        return fail("relocation reference did not prefer explicit global scope");
    }

    // A symbolic requester with no local definition still falls through to
    // the same explicit global scope before its graph-local dependencies.
    graph.objects[0].linker_metadata.symbolic = true;
    const auto symbolic = resolve_elf32_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!symbolic || symbolic.resolution.entries.size() != 1 ||
        !symbolic.resolution.entries[0].reference.has_value()) {
        return fail("symbolic requester did not fall through to global scope");
    }
    const auto& symbolic_reference =
        *symbolic.resolution.entries[0].reference;
    if (!symbolic_reference.defining_object_index.has_value() ||
        *symbolic_reference.defining_object_index != 1 ||
        symbolic_reference.symbol_value != 0x5120) {
        return fail("symbolic relocation fallback changed global ordering");
    }

    // When the requester also defines the relocation symbol, ordinary lookup
    // still honors the explicit global group, while DT_SYMBOLIC must bind the
    // requester's own definition first.
    if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                             0x1000, 1, 1, 0, 1, 0x90)) {
        return fail("could not stage requester-local relocation definition");
    }
    graph.objects[0].linker_metadata.rel_table =
        Elf32RelTableMetadata{
            .guest_address = kRelTable,
            .size = 8,
            .entry_size = 8,
        };
    graph.objects[0].linker_metadata.symbolic = false;
    const auto defined_ordinary = resolve_elf32_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!defined_ordinary ||
        defined_ordinary.resolution.entries.size() != 1 ||
        !defined_ordinary.resolution.entries[0].reference.has_value()) {
        return fail("ordinary defined reference did not resolve exactly once");
    }
    const auto& defined_ordinary_reference =
        *defined_ordinary.resolution.entries[0].reference;
    if (!defined_ordinary_reference.defining_object_index.has_value() ||
        *defined_ordinary_reference.defining_object_index != 1 ||
        defined_ordinary_reference.symbol_value != 0x5120) {
        return fail("ordinary defined reference did not preserve global-first ordering");
    }

    graph.objects[0].linker_metadata.symbolic = true;
    const auto defined_symbolic = resolve_elf32_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!defined_symbolic ||
        defined_symbolic.resolution.entries.size() != 1 ||
        !defined_symbolic.resolution.entries[0].reference.has_value()) {
        return fail("symbolic defined reference did not resolve exactly once");
    }
    const auto& defined_symbolic_reference =
        *defined_symbolic.resolution.entries[0].reference;
    if (!defined_symbolic_reference.defining_object_index.has_value() ||
        *defined_symbolic_reference.defining_object_index != 0 ||
        defined_symbolic_reference.symbol_value != 0x1090) {
        return fail("symbolic defined reference did not bind requester first");
    }

    // The PLT resolver shares the same reference policy. Prove both ordinary
    // global-first and symbolic requester-first ordering through JUMP_SLOT.
    graph.objects[0].linker_metadata.plt_rel_table =
        Elf32RelTableMetadata{
            .guest_address = kPltRelTable,
            .size = 8,
            .entry_size = 8,
        };
    if (!write_rel_at(memory, kPltRelTable, 0, 0x11004, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, 0x12004, 0)) {
        return fail("could not stage PLT scope-policy relocation");
    }

    graph.objects[0].linker_metadata.symbolic = false;
    const auto plt_ordinary = resolve_elf32_plt_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!plt_ordinary ||
        plt_ordinary.resolution.entries.size() != 1 ||
        !plt_ordinary.resolution.entries[0].reference.has_value()) {
        return fail("ordinary PLT scope-policy reference did not resolve exactly once");
    }
    const auto& plt_ordinary_reference =
        *plt_ordinary.resolution.entries[0].reference;
    if (!plt_ordinary_reference.defining_object_index.has_value() ||
        *plt_ordinary_reference.defining_object_index != 1 ||
        plt_ordinary_reference.symbol_value != 0x5120) {
        return fail("ordinary PLT reference did not preserve global-first ordering");
    }

    graph.objects[0].linker_metadata.symbolic = true;
    const auto plt_symbolic = resolve_elf32_plt_rel_relocation_references(
        memory, graph, 0, scoped);
    if (!plt_symbolic ||
        plt_symbolic.resolution.entries.size() != 1 ||
        !plt_symbolic.resolution.entries[0].reference.has_value()) {
        return fail("symbolic PLT scope-policy reference did not resolve exactly once");
    }
    const auto& plt_symbolic_reference =
        *plt_symbolic.resolution.entries[0].reference;
    if (!plt_symbolic_reference.defining_object_index.has_value() ||
        *plt_symbolic_reference.defining_object_index != 0 ||
        plt_symbolic_reference.symbol_value != 0x1090) {
        return fail("symbolic PLT reference did not bind requester first");
    }
    return 0;
}

int expect_reference_form_error(std::uint8_t binding,
                                std::uint8_t type,
                                std::uint8_t other,
                                std::uint16_t section_index,
                                Elf32RelocationResolveError expected) {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, kRelTable, 8);
    if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                             0x1000, binding, type, other, section_index) ||
        !write_rel(memory, 0, 0x11000, 1, kRArmAbs32) ||
        !write_u32(memory, 0x12000, 0)) {
        return fail("could not stage unsupported reference form");
    }
    const auto result = resolve_elf32_rel_relocation_references(
        memory, graph, 0, options());
    if (result.error != expected) {
        return fail("unsupported relocation reference form returned wrong error");
    }
    return 0;
}

int test_reference_validation_failures() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 0, kRArmAbs32) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage missing reference-symbol case");
        }
        if (resolve_elf32_rel_relocation_references(
                memory, graph, 0, options()).error !=
            Elf32RelocationResolveError::MissingReferenceSymbol) {
            return fail("symbol index zero was not rejected for ABS32");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 2, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage symbol-index bounds case");
        }
        if (resolve_elf32_rel_relocation_references(
                memory, graph, 0, options()).error !=
            Elf32RelocationResolveError::SymbolIndexOutOfRange) {
            return fail("relocation symbol index extent was not enforced");
        }
    }

    if (expect_reference_form_error(
            0, 1, 0, 0,
            Elf32RelocationResolveError::UnsupportedReferenceBinding) != 0 ||
        expect_reference_form_error(
            1, 1, 3, 0,
            Elf32RelocationResolveError::UnsupportedReferenceVisibility) != 0 ||
        expect_reference_form_error(
            1, 1, 2, 0,
            Elf32RelocationResolveError::UnsupportedReferenceVisibility) != 0 ||
        expect_reference_form_error(
            1, 1, 4, 0,
            Elf32RelocationResolveError::UnsupportedReferenceVisibility) != 0 ||
        expect_reference_form_error(
            1, 6, 0, 0,
            Elf32RelocationResolveError::UnsupportedReferenceType) != 0 ||
        expect_reference_form_error(
            1, 10, 0, 0,
            Elf32RelocationResolveError::UnsupportedReferenceType) != 0 ||
        expect_reference_form_error(
            1, 1, 0, 0xfff2,
            Elf32RelocationResolveError::UnsupportedReferenceSection) != 0 ||
        expect_reference_form_error(
            1, 1, 0, 0xffff,
            Elf32RelocationResolveError::UnsupportedReferenceSection) != 0) {
        return 1;
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage versioned reference case");
        }
        graph.objects[0].linker_metadata.has_symbol_versioning = true;
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error != Elf32RelocationResolveError::SymbolLookupFailed ||
            result.lookup_error !=
                liba32android::elf::Elf32SymbolLookupError::InvalidVersionMetadata) {
            return fail("inconsistent relocation version marker was not rejected");
        }
    }
    return 0;
}

int test_reference_nested_failures() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmAbs32) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage missing-hash index failure");
        }
        graph.objects[0].linker_metadata.sysv_hash_table.reset();
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error != Elf32RelocationResolveError::IndexBuildFailed ||
            result.index_error != Elf32SymbolIndexError::MissingHashTable) {
            return fail("symbol-index build failure was not retained");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0, 0, 0x90) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage reference-name failure");
        }
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error != Elf32RelocationResolveError::ReferenceNameFailed ||
            result.string_error !=
                Elf32LinkerStringError::StringOffsetOutOfRange) {
            return fail("reference string-table failure was not retained");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, kRelTable, 8);
        graph.objects.resize(2);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 1, 0, 0) ||
            !stage_symbol_object(memory, graph.objects[1], 1, "other",
                                 0x5000, 1, 1, 0, 1, 0x80) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage nested graph lookup failure");
        }
        graph.objects[1].linker_metadata.sysv_hash_table.reset();
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
        };
        const auto result = resolve_elf32_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error != Elf32RelocationResolveError::SymbolLookupFailed ||
            result.graph_error !=
                liba32android::elf::Elf32GraphSymbolLookupError::IndexBuildFailed ||
            result.index_error != Elf32SymbolIndexError::MissingHashTable ||
            !result.failing_object.has_value() ||
            *result.failing_object != 1) {
            return fail("nested graph/index lookup failure was not retained");
        }
    }
    return 0;
}

int test_duplicate_target_rejected_without_mutation() {
    LinearGuestMemory memory(0x4000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, kRelTable, 16);
    if (!write_rel(memory, 0, 0x1000, 1, kRArmAbs32) ||
        !write_rel(memory, 1, 0x1000, 2, kRArmGlobDat) ||
        !write_u32(memory, 0x2000, 0xdeadbeefU)) {
        return fail("could not stage duplicate-target test");
    }

    const auto result =
        build_elf32_rel_relocation_plan(memory, graph, 0, options());
    if (result.error != Elf32RelocationPlanError::DuplicateTarget) {
        return fail("duplicate writable relocation target was not rejected");
    }

    std::uint32_t value = 0;
    if (!read_u32(memory, 0x2000, value) || value != 0xdeadbeefU) {
        return fail("duplicate-target planning failure mutated guest memory");
    }
    return 0;
}

int test_plt_empty_and_exact_decode() {
    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        const auto result =
            build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options(0));
        if (!result || !result.plan.entries.empty()) {
            return fail("object without PLT REL table did not produce empty success");
        }
    }

    LinearGuestMemory memory(0x5000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 16);
    if (!write_rel_at(memory, kPltRelTable, 0, 0x1000, 1,
                      kRArmJumpSlot) ||
        !write_rel_at(memory, kPltRelTable, 1, 0x1004, 2,
                      kRArmJumpSlot) ||
        !write_u32(memory, 0x2000, 0x11223344U) ||
        !write_u32(memory, 0x2004, 0xaabbccddU)) {
        return fail("could not stage valid PLT REL plan");
    }

    const auto result =
        build_elf32_plt_rel_relocation_plan(memory, graph, 0, options());
    if (!result || result.plan.object_index != 0 ||
        result.plan.entries.size() != 2) {
        return fail("valid PLT REL table did not decode");
    }
    const auto& first = result.plan.entries[0];
    const auto& second = result.plan.entries[1];
    if (first.offset != 0x1000 || first.symbol_index != 1 ||
        first.type != kRArmJumpSlot ||
        first.place_guest_address != 0x2000 ||
        !first.original_word.has_value() ||
        *first.original_word != 0x11223344U ||
        second.offset != 0x1004 || second.symbol_index != 2 ||
        second.type != kRArmJumpSlot ||
        second.place_guest_address != 0x2004 ||
        !second.original_word.has_value() ||
        *second.original_word != 0xaabbccddU) {
        return fail("PLT JUMP_SLOT plan metadata was incorrect");
    }

    std::uint32_t value = 0;
    if (!read_u32(memory, 0x2000, value) || value != 0x11223344U ||
        !read_u32(memory, 0x2004, value) || value != 0xaabbccddU) {
        return fail("read-only PLT planning mutated target words");
    }
    return 0;
}

int test_plt_plan_failures() {
    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 16);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x1000, 1,
                          kRArmJumpSlot) ||
            !write_rel_at(memory, kPltRelTable, 1, 0x1004, 2,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x2000, 1) ||
            !write_u32(memory, 0x2004, 2)) {
            return fail("could not stage PLT limit test");
        }
        if (build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options(0)).error !=
            Elf32RelocationPlanError::InvalidOptions) {
            return fail("zero relocation limit with PLT table was not rejected");
        }
        if (build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options(1)).error !=
            Elf32RelocationPlanError::TooManyRelocations) {
            return fail("PLT relocation count ceiling was not enforced");
        }
        graph.objects[0].linker_metadata.plt_rel_table->entry_size = 4;
        if (build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::RelocationReadFailed) {
            return fail("crafted invalid PLT REL entry size was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x2000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x3000, 1,
                          kRArmGlobDat)) {
            return fail("could not stage PLT unsupported-type test");
        }
        if (build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::UnsupportedRelocationType) {
            return fail("non-JUMP_SLOT PLT type was not rejected before target access");
        }
    }

    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph =
            graph_with_plt_rel(0xfffffff0U, kPltRelTable, 8);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x20, 1,
                          kRArmJumpSlot) ||
            build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
                Elf32RelocationPlanError::PlaceOverflow) {
            return fail("PLT place overflow was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x1001, 1,
                          kRArmJumpSlot) ||
            build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
                Elf32RelocationPlanError::UnalignedPlace) {
            return fail("unaligned PLT target was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x2000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x3000, 1,
                          kRArmJumpSlot) ||
            build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
                Elf32RelocationPlanError::TargetReadFailed) {
            return fail("unreadable PLT target was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x4000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 16);
        if (!write_rel_at(memory, kPltRelTable, 0, 0x1000, 1,
                          kRArmJumpSlot) ||
            !write_rel_at(memory, kPltRelTable, 1, 0x1000, 2,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x2000, 0xfeedfaceU)) {
            return fail("could not stage PLT duplicate-target test");
        }
        if (build_elf32_plt_rel_relocation_plan(
                memory, graph, 0, options()).error !=
            Elf32RelocationPlanError::DuplicateTarget) {
            return fail("duplicate PLT target was not rejected");
        }
        std::uint32_t value = 0;
        if (!read_u32(memory, 0x2000, value) ||
            value != 0xfeedfaceU) {
            return fail("PLT duplicate-target failure mutated target");
        }
    }
    return 0;
}

int test_plt_reference_resolution() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        graph.objects.resize(2);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 2, 0, 0) ||
            !stage_symbol_object(memory, graph.objects[1], 1, "target",
                                 0x5000, 1, 2, 0, 1, 0x120) ||
            !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x12000, 0xdeadbeefU)) {
            return fail("could not stage graph-local PLT reference");
        }
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
        };
        const auto result = resolve_elf32_plt_rel_relocation_references(
            memory, graph, 0, options());
        if (!result || result.resolution.entries.size() != 1 ||
            !result.resolution.entries[0].reference.has_value()) {
            return fail("valid PLT reference did not resolve");
        }
        const auto& reference = *result.resolution.entries[0].reference;
        if (reference.name != "target" ||
            reference.symbol_index != 1 ||
            reference.symbol_value != 0x5120 ||
            reference.unresolved_weak ||
            !reference.defining_object_index.has_value() ||
            *reference.defining_object_index != 1) {
            return fail("graph-local PLT reference result was incorrect");
        }
        std::uint32_t target = 0;
        if (!read_u32(memory, 0x12000, target) ||
            target != 0xdeadbeefU) {
            return fail("PLT reference resolution mutated target memory");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "missing",
                                 0x1000, 2, 2, 0, 0) ||
            !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x12000, 0x12345678U)) {
            return fail("could not stage unresolved weak PLT reference");
        }
        const auto result = resolve_elf32_plt_rel_relocation_references(
            memory, graph, 0, options());
        if (!result || result.resolution.entries.size() != 1 ||
            !result.resolution.entries[0].reference.has_value() ||
            !result.resolution.entries[0].reference->unresolved_weak ||
            result.resolution.entries[0].reference->symbol_value != 0) {
            return fail("unresolved weak PLT reference did not become S=0");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "missing",
                                 0x1000, 1, 2, 0, 0) ||
            !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage unresolved strong PLT reference");
        }
        const auto result = resolve_elf32_plt_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error !=
            Elf32RelocationResolveError::UnresolvedStrongSymbol) {
            return fail("unresolved strong PLT reference was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 2, 0, 0) ||
            !write_rel_at(memory, kPltRelTable, 0, 0x11000, 0,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage zero-symbol PLT reference");
        }
        if (resolve_elf32_plt_rel_relocation_references(
                memory, graph, 0, options()).error !=
            Elf32RelocationResolveError::MissingReferenceSymbol) {
            return fail("PLT symbol index zero was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_plt_rel(0x1000, kPltRelTable, 8);
        if (!stage_symbol_object(memory, graph.objects[0], 0, "target",
                                 0x1000, 1, 2, 0, 0) ||
            !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                          kRArmJumpSlot) ||
            !write_u32(memory, 0x12000, 0)) {
            return fail("could not stage versioned PLT reference");
        }
        graph.objects[0].linker_metadata.has_symbol_versioning = true;
        const auto result = resolve_elf32_plt_rel_relocation_references(
            memory, graph, 0, options());
        if (result.error != Elf32RelocationResolveError::SymbolLookupFailed ||
            result.lookup_error !=
                liba32android::elf::Elf32SymbolLookupError::InvalidVersionMetadata) {
            return fail("inconsistent PLT version marker was not rejected");
        }
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_empty_and_exact_decode(); status != 0) return status;
    if (const int status =
            test_relocation_table_reads_are_batched_and_preserve_error_order();
        status != 0) return status;
    if (const int status = test_limits_and_graph_inputs(); status != 0) return status;
    if (const int status = test_read_place_and_type_failures(); status != 0) return status;
    if (const int status = test_reference_resolution_and_weak_behavior(); status != 0) return status;
    if (const int status = test_reference_scope_policy_flows_through_relocation(); status != 0) return status;
    if (const int status = test_reference_validation_failures(); status != 0) return status;
    if (const int status = test_reference_nested_failures(); status != 0) return status;
    if (const int status = test_duplicate_target_rejected_without_mutation(); status != 0) return status;
    if (const int status = test_plt_empty_and_exact_decode(); status != 0) return status;
    if (const int status = test_plt_plan_failures(); status != 0) return status;
    if (const int status = test_plt_reference_resolution(); status != 0) return status;
    return 0;
}
