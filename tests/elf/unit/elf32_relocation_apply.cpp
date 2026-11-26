#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "elf/elf32_relocation.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32HashTableMetadata;
using liba32android::elf::Elf32RelTableMetadata;
using liba32android::elf::Elf32RelocationApplyError;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32RelocationTable;
using liba32android::elf::Elf32StringTableMetadata;
using liba32android::elf::Elf32SymbolTableMetadata;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::apply_elf32_plt_rel_relocations;
using liba32android::elf::apply_elf32_rel_relocations;
using liba32android::elf::kRArmAbs32;
using liba32android::elf::kRArmRel32;
using liba32android::elf::kRArmGlobDat;
using liba32android::elf::kRArmJumpSlot;
using liba32android::elf::kRArmNone;
using liba32android::elf::kRArmRelative;
using liba32android::memory::GuestMemory;
using liba32android::memory::LinearGuestMemory;

constexpr std::uint32_t kMemoryBase = 0x1000;
constexpr std::uint32_t kRelTable = 0x1100;
constexpr std::uint32_t kPltRelTable = 0x1180;
constexpr std::uint32_t kTarget0 = 0x12000;
constexpr std::uint32_t kTarget1 = 0x12004;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u32(GuestMemory& memory,
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

bool read_u32(const GuestMemory& memory,
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

bool write_rel_at(GuestMemory& memory,
                  std::uint32_t table,
                  std::uint32_t index,
                  std::uint32_t offset,
                  std::uint32_t symbol_index,
                  std::uint8_t type) {
    return write_u32(memory, table + index * 8U, offset) &&
           write_u32(memory, table + index * 8U + 4U,
                     (symbol_index << 8U) | type);
}

bool write_rel(GuestMemory& memory,
               std::uint32_t index,
               std::uint32_t offset,
               std::uint32_t symbol_index,
               std::uint8_t type) {
    return write_rel_at(
        memory, kRelTable, index, offset, symbol_index, type);
}

Elf32DependencyGraph graph_with_rel(std::uint32_t load_bias,
                                    std::uint32_t count) {
    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    graph.objects[0].identity = "relocation-apply-test";
    graph.objects[0].load.load_bias = load_bias;
    graph.objects[0].linker_metadata.rel_table =
        Elf32RelTableMetadata{
            .guest_address = kRelTable,
            .size = count * 8U,
            .entry_size = 8,
        };
    return graph;
}

Elf32DependencyGraph graph_with_plt_rel(std::uint32_t load_bias,
                                        std::uint32_t count) {
    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    graph.objects[0].identity = "plt-relocation-apply-test";
    graph.objects[0].load.load_bias = load_bias;
    graph.objects[0].linker_metadata.plt_rel_table =
        Elf32RelTableMetadata{
            .guest_address = kPltRelTable,
            .size = count * 8U,
            .entry_size = 8,
        };
    return graph;
}

Elf32DependencyGraph graph_with_both(std::uint32_t load_bias,
                                     std::uint32_t rel_count,
                                     std::uint32_t plt_count) {
    Elf32DependencyGraph graph = graph_with_rel(load_bias, rel_count);
    graph.objects[0].identity = "combined-relocation-apply-test";
    graph.objects[0].linker_metadata.plt_rel_table = Elf32RelTableMetadata{
        .guest_address = kPltRelTable,
        .size = plt_count * 8U,
        .entry_size = 8,
    };
    return graph;
}

Elf32RelocationOptions options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16;
    result.symbols.max_symbols = 16;
    result.symbols.max_hash_buckets = 8;
    result.symbols.max_gnu_bloom_words = 8;
    result.symbols.max_scope_objects = 8;
    result.symbols.max_name_bytes = 64;
    return result;
}

bool write_string_at(GuestMemory& memory,
                     std::uint32_t table,
                     std::uint32_t offset,
                     std::string_view value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    return memory.write(table + offset, bytes);
}

bool write_symbol_at(GuestMemory& memory,
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

bool stage_symbol(GuestMemory& memory,
                  liba32android::elf::Elf32LoadedDependencyObject& object,
                  std::string_view name,
                  std::uint32_t load_bias,
                  std::uint32_t value,
                  std::uint8_t binding,
                  std::uint16_t section_index,
                  std::uint8_t type = 1) {
    constexpr std::uint32_t strings = 0x4000;
    constexpr std::uint32_t symbols = 0x4100;
    constexpr std::uint32_t hash = 0x4300;

    object.load.load_bias = load_bias;
    object.linker_metadata.string_table =
        Elf32StringTableMetadata{.guest_address = strings, .size = 0x80};
    object.linker_metadata.symbol_table =
        Elf32SymbolTableMetadata{.guest_address = symbols, .entry_size = 16};
    object.linker_metadata.sysv_hash_table =
        Elf32HashTableMetadata{.guest_address = hash};

    return write_string_at(memory, strings, 1, name) &&
           write_symbol_at(memory, symbols, 1, 1, value,
                           binding, type, 0, section_index) &&
           write_u32(memory, hash, 1) &&
           write_u32(memory, hash + 4U, 2) &&
           write_u32(memory, hash + 8U, 1) &&
           write_u32(memory, hash + 12U, 0) &&
           write_u32(memory, hash + 16U, 0);
}

class ScriptedWriteMemory final : public GuestMemory {
public:
    ScriptedWriteMemory(std::size_t size, std::uint32_t base)
        : backing_(size, base) {}

    void arm_failures(std::vector<std::size_t> failures) {
        failures_ = std::move(failures);
        write_calls_ = 0;
        armed_ = true;
    }

    [[nodiscard]] bool read(
        std::uint32_t address,
        std::span<std::uint8_t> output) const override {
        return backing_.read(address, output);
    }

    [[nodiscard]] bool write(
        std::uint32_t address,
        std::span<const std::uint8_t> input) override {
        if (armed_) {
            ++write_calls_;
            if (std::find(failures_.begin(), failures_.end(),
                          write_calls_) != failures_.end()) {
                return false;
            }
        }
        return backing_.write(address, input);
    }

private:
    LinearGuestMemory backing_;
    std::vector<std::size_t> failures_;
    std::size_t write_calls_{};
    bool armed_{};
};

int test_none_and_relative() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 2);

    if (!write_rel(memory, 0, 0x1000, 99, kRArmNone) ||
        !write_rel(memory, 1, 0x11000, 0, kRArmRelative) ||
        !write_u32(memory, 0x2000, 0xaabbccddU) ||
        !write_u32(memory, kTarget0, 0xfffffff0U)) {
        return fail("could not stage NONE/RELATIVE apply case");
    }

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    if (!result || result.application.writes.size() != 1 ||
        result.application.writes[0].type != kRArmRelative ||
        result.application.writes[0].original_word != 0xfffffff0U ||
        result.application.writes[0].final_word != 0x00000ff0U) {
        return fail("NONE/RELATIVE application result was incorrect");
    }

    std::uint32_t value = 0;
    if (!read_u32(memory, 0x2000, value) ||
        value != 0xaabbccddU ||
        !read_u32(memory, kTarget0, value) ||
        value != 0x00000ff0U) {
        return fail("NONE wrote or RELATIVE formula was incorrect");
    }
    return 0;
}

int test_glob_dat_ignores_addend() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "target",
                      0x1000, 0x234, 1, 1) ||
        !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
        !write_u32(memory, kTarget0, 0x11111111U)) {
        return fail("could not stage GLOB_DAT apply case");
    }

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (!result || result.application.writes.size() != 1 ||
        result.application.writes[0].final_word != 0x1234U ||
        !read_u32(memory, kTarget0, value) ||
        value != 0x1234U) {
        return fail("GLOB_DAT did not write S while ignoring nonzero A");
    }
    return 0;
}

int test_abs32_wraps_modulo_32() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "absolute",
                      0x1000, 0xfffffff0U, 1, 0xfff1) ||
        !write_rel(memory, 0, 0x11000, 1, kRArmAbs32) ||
        !write_u32(memory, kTarget0, 0x30U)) {
        return fail("could not stage ABS32 wrap case");
    }

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (!result || result.application.writes.size() != 1 ||
        result.application.writes[0].final_word != 0x20U ||
        !read_u32(memory, kTarget0, value) || value != 0x20U) {
        return fail("ABS32 did not apply S+A modulo 2^32");
    }
    return 0;
}


int test_rel32_formula_thumb_and_weak() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, 1);
        if (!stage_symbol(memory, graph.objects[0], "target",
                          0x1000, 0x234, 1, 1, 1) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmRel32) ||
            !write_u32(memory, kTarget0, 0x20U)) {
            return fail("could not stage ARM REL32 case");
        }
        const auto result =
            apply_elf32_rel_relocations(memory, graph, 0, options());
        const std::uint32_t expected =
            static_cast<std::uint32_t>((0x1234U + 0x20U) - kTarget0);
        std::uint32_t value = 0;
        if (!result || result.application.writes.size() != 1 ||
            result.application.writes[0].type != kRArmRel32 ||
            result.application.writes[0].final_word != expected ||
            !read_u32(memory, kTarget0, value) || value != expected) {
            return fail("ARM REL32 formula was incorrect");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, 1);
        if (!stage_symbol(memory, graph.objects[0], "thumb_target",
                          0x1000, 0x235, 1, 1, 2) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmRel32) ||
            !write_u32(memory, kTarget0, 1U)) {
            return fail("could not stage Thumb REL32 case");
        }
        const auto result =
            apply_elf32_rel_relocations(memory, graph, 0, options());
        // st_value 0x235 identifies a Thumb STT_FUNC. Relocation S is 0x1234,
        // T is 1, so ((S + A) | T) - P must use 0x1235 rather than 0x1237.
        const std::uint32_t expected =
            static_cast<std::uint32_t>((0x1235U) - kTarget0);
        std::uint32_t value = 0;
        if (!result || result.application.writes.size() != 1 ||
            result.application.writes[0].final_word != expected ||
            !read_u32(memory, kTarget0, value) || value != expected) {
            return fail("Thumb REL32 did not apply the AAELF32 T bit correctly");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        auto graph = graph_with_rel(0x1000, 1);
        if (!stage_symbol(memory, graph.objects[0], "missing",
                          0x1000, 0, 2, 0, 2) ||
            !write_rel(memory, 0, 0x11000, 1, kRArmRel32) ||
            !write_u32(memory, kTarget0, 4U)) {
            return fail("could not stage weak REL32 case");
        }
        const auto result =
            apply_elf32_rel_relocations(memory, graph, 0, options());
        const std::uint32_t expected =
            static_cast<std::uint32_t>(4U - kTarget0);
        std::uint32_t value = 0;
        if (!result || result.application.writes.size() != 1 ||
            result.application.writes[0].final_word != expected ||
            !read_u32(memory, kTarget0, value) || value != expected) {
            return fail("unresolved weak REL32 did not use S=0 and T=0");
        }
    }

    return 0;
}

int test_invalid_relative_is_prewrite_failure() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 2);
    if (!write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel(memory, 1, 0x11004, 1, kRArmRelative) ||
        !write_u32(memory, kTarget0, 0x10U) ||
        !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage invalid RELATIVE case");
    }

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (result.error != Elf32RelocationApplyError::InvalidRelativeSymbol ||
        !result.failing_relocation.has_value() ||
        *result.failing_relocation != 1 ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, first) || first != 0x10U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("invalid RELATIVE did not fail before all writes");
    }
    return 0;
}

int test_resolve_failure_is_prewrite() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "missing",
                      0x1000, 0, 1, 0) ||
        !write_rel(memory, 0, 0x11000, 1, kRArmGlobDat) ||
        !write_u32(memory, kTarget0, 0xfeedfaceU)) {
        return fail("could not stage resolve-failure apply case");
    }

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (result.error != Elf32RelocationApplyError::ResolveFailed ||
        result.resolution_failure.error !=
            liba32android::elf::Elf32RelocationResolveError::UnresolvedStrongSymbol ||
        !read_u32(memory, kTarget0, value) ||
        value != 0xfeedfaceU) {
        return fail("resolution failure mutated guest memory");
    }
    return 0;
}

int test_late_write_failure_rolls_back() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 2);
    if (!write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel(memory, 1, 0x11004, 0, kRArmRelative) ||
        !write_u32(memory, kTarget0, 0x10U) ||
        !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage rollback case");
    }
    memory.arm_failures({2});

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (result.error != Elf32RelocationApplyError::TargetWriteFailed ||
        result.primary_error != Elf32RelocationApplyError::TargetWriteFailed ||
        !result.failing_relocation.has_value() ||
        *result.failing_relocation != 1 ||
        result.rollback_failing_relocation.has_value() ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, first) || first != 0x10U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("late write failure did not restore prior relocation");
    }
    return 0;
}

int test_rollback_failure_is_explicit() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_rel(0x1000, 2);
    if (!write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel(memory, 1, 0x11004, 0, kRArmRelative) ||
        !write_u32(memory, kTarget0, 0x10U) ||
        !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage rollback-failure case");
    }
    memory.arm_failures({2, 3});

    const auto result =
        apply_elf32_rel_relocations(memory, graph, 0, options());
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (result.error != Elf32RelocationApplyError::RollbackFailed ||
        result.primary_error != Elf32RelocationApplyError::TargetWriteFailed ||
        !result.failing_relocation.has_value() ||
        *result.failing_relocation != 1 ||
        !result.rollback_failing_relocation.has_value() ||
        *result.rollback_failing_relocation != 0 ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, first) || first != 0x1010U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("rollback failure was not explicit/preserved");
    }
    return 0;
}


int test_jump_slot_ignores_original_word() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "target",
                      0x1000, 0x234, 1, 1, 2) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x11111111U)) {
        return fail("could not stage JUMP_SLOT apply case");
    }

    const auto result =
        apply_elf32_plt_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (!result || result.application.writes.size() != 1 ||
        result.application.writes[0].type != kRArmJumpSlot ||
        result.application.writes[0].original_word != 0x11111111U ||
        result.application.writes[0].final_word != 0x1234U ||
        !read_u32(memory, kTarget0, value) ||
        value != 0x1234U) {
        return fail("JUMP_SLOT did not write S while ignoring original word");
    }
    return 0;
}

int test_jump_slot_weak_zero() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "missing",
                      0x1000, 0, 2, 0, 2) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0xdeadbeefU)) {
        return fail("could not stage weak JUMP_SLOT case");
    }

    const auto result =
        apply_elf32_plt_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 1;
    if (!result || result.application.writes.size() != 1 ||
        result.application.writes[0].final_word != 0 ||
        !read_u32(memory, kTarget0, value) || value != 0) {
        return fail("unresolved weak JUMP_SLOT did not write zero");
    }
    return 0;
}

int test_jump_slot_resolve_failure_is_prewrite() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, 1);
    if (!stage_symbol(memory, graph.objects[0], "missing",
                      0x1000, 0, 1, 0, 2) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0xfeedfaceU)) {
        return fail("could not stage strong JUMP_SLOT resolve failure");
    }

    const auto result =
        apply_elf32_plt_rel_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (result.error != Elf32RelocationApplyError::ResolveFailed ||
        result.resolution_failure.error !=
            liba32android::elf::Elf32RelocationResolveError::UnresolvedStrongSymbol ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, value) ||
        value != 0xfeedfaceU) {
        return fail("JUMP_SLOT resolution failure mutated guest memory");
    }
    return 0;
}

int test_plt_late_write_failure_rolls_back() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, 2);
    if (!stage_symbol(memory, graph.objects[0], "target",
                      0x1000, 0x234, 1, 1, 2) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                      kRArmJumpSlot) ||
        !write_rel_at(memory, kPltRelTable, 1, 0x11004, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) ||
        !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage PLT rollback case");
    }
    memory.arm_failures({2});

    const auto result =
        apply_elf32_plt_rel_relocations(memory, graph, 0, options());
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (result.error != Elf32RelocationApplyError::TargetWriteFailed ||
        result.primary_error != Elf32RelocationApplyError::TargetWriteFailed ||
        !result.failing_relocation.has_value() ||
        *result.failing_relocation != 1 ||
        result.rollback_failing_relocation.has_value() ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, first) || first != 0x10U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("PLT late write failure did not restore prior slot");
    }
    return 0;
}

int test_plt_rollback_failure_is_explicit() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_plt_rel(0x1000, 2);
    if (!stage_symbol(memory, graph.objects[0], "target",
                      0x1000, 0x234, 1, 1, 2) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1,
                      kRArmJumpSlot) ||
        !write_rel_at(memory, kPltRelTable, 1, 0x11004, 1,
                      kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) ||
        !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage PLT rollback-failure case");
    }
    memory.arm_failures({2, 3});

    const auto result =
        apply_elf32_plt_rel_relocations(memory, graph, 0, options());
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (result.error != Elf32RelocationApplyError::RollbackFailed ||
        result.primary_error != Elf32RelocationApplyError::TargetWriteFailed ||
        !result.failing_relocation.has_value() ||
        *result.failing_relocation != 1 ||
        !result.rollback_failing_relocation.has_value() ||
        *result.rollback_failing_relocation != 0 ||
        !result.application.writes.empty() ||
        !read_u32(memory, kTarget0, first) || first != 0x1234U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("PLT rollback failure was not explicit/preserved");
    }
    return 0;
}


int test_combined_applies_main_then_plt() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_both(0x1000, 1, 1);
    if (!stage_symbol(memory, graph.objects[0], "target", 0x1000, 0x234, 1, 1, 2) ||
        !write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11004, 1, kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) || !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage combined success case");
    }
    const auto result = apply_elf32_combined_relocations(memory, graph, 0, options());
    std::uint32_t first = 0, second = 0;
    if (!result || result.application.writes.size() != 2 ||
        result.application.writes[0].table != Elf32RelocationTable::MainRel ||
        result.application.writes[0].final_word != 0x1010U ||
        result.application.writes[1].table != Elf32RelocationTable::PltRel ||
        result.application.writes[1].final_word != 0x1234U ||
        !read_u32(memory, kTarget0, first) || first != 0x1010U ||
        !read_u32(memory, kTarget1, second) || second != 0x1234U) {
        return fail("combined transaction did not apply main then PLT");
    }
    return 0;
}

int test_combined_plt_prepare_failure_is_prewrite() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_both(0x1000, 1, 1);
    if (!stage_symbol(memory, graph.objects[0], "missing", 0x1000, 0, 1, 0, 2) ||
        !write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11004, 1, kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) || !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage combined prepare failure");
    }
    const auto result = apply_elf32_combined_relocations(memory, graph, 0, options());
    std::uint32_t first = 0, second = 0;
    if (result.error != Elf32RelocationApplyError::ResolveFailed ||
        !result.failing_table.has_value() || *result.failing_table != Elf32RelocationTable::PltRel ||
        !read_u32(memory, kTarget0, first) || first != 0x10U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("PLT prepare failure mutated combined targets");
    }
    return 0;
}

int test_combined_duplicate_target_is_prewrite() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_both(0x1000, 1, 1);
    if (!stage_symbol(memory, graph.objects[0], "target", 0x1000, 0x234, 1, 1, 2) ||
        !write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11000, 1, kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U)) {
        return fail("could not stage combined duplicate");
    }
    const auto result = apply_elf32_combined_relocations(memory, graph, 0, options());
    std::uint32_t value = 0;
    if (result.error != Elf32RelocationApplyError::DuplicateTargetAcrossTables ||
        !result.failing_table.has_value() || *result.failing_table != Elf32RelocationTable::PltRel ||
        !read_u32(memory, kTarget0, value) || value != 0x10U) {
        return fail("cross-table duplicate target was not rejected prewrite");
    }
    return 0;
}

int test_combined_plt_write_failure_rolls_back_main() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_both(0x1000, 1, 1);
    if (!stage_symbol(memory, graph.objects[0], "target", 0x1000, 0x234, 1, 1, 2) ||
        !write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11004, 1, kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) || !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage combined rollback");
    }
    memory.arm_failures({2});
    const auto result = apply_elf32_combined_relocations(memory, graph, 0, options());
    std::uint32_t first = 0, second = 0;
    if (result.error != Elf32RelocationApplyError::TargetWriteFailed ||
        !result.failing_table.has_value() || *result.failing_table != Elf32RelocationTable::PltRel ||
        result.rollback_failing_table.has_value() ||
        !read_u32(memory, kTarget0, first) || first != 0x10U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("PLT failure did not roll back main write");
    }
    return 0;
}

int test_combined_cross_table_rollback_failure_is_explicit() {
    ScriptedWriteMemory memory(0x20000, kMemoryBase);
    auto graph = graph_with_both(0x1000, 1, 1);
    if (!stage_symbol(memory, graph.objects[0], "target", 0x1000, 0x234, 1, 1, 2) ||
        !write_rel(memory, 0, 0x11000, 0, kRArmRelative) ||
        !write_rel_at(memory, kPltRelTable, 0, 0x11004, 1, kRArmJumpSlot) ||
        !write_u32(memory, kTarget0, 0x10U) || !write_u32(memory, kTarget1, 0x20U)) {
        return fail("could not stage combined rollback failure");
    }
    memory.arm_failures({2, 3});
    const auto result = apply_elf32_combined_relocations(memory, graph, 0, options());
    std::uint32_t first = 0, second = 0;
    if (result.error != Elf32RelocationApplyError::RollbackFailed ||
        !result.failing_table.has_value() || *result.failing_table != Elf32RelocationTable::PltRel ||
        !result.rollback_failing_table.has_value() ||
        *result.rollback_failing_table != Elf32RelocationTable::MainRel ||
        !read_u32(memory, kTarget0, first) || first != 0x1010U ||
        !read_u32(memory, kTarget1, second) || second != 0x20U) {
        return fail("cross-table rollback failure was not explicit");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_none_and_relative(); status != 0) return status;
    if (const int status = test_glob_dat_ignores_addend(); status != 0) return status;
    if (const int status = test_abs32_wraps_modulo_32(); status != 0) return status;
    if (const int status = test_rel32_formula_thumb_and_weak(); status != 0) return status;
    if (const int status = test_invalid_relative_is_prewrite_failure(); status != 0) return status;
    if (const int status = test_resolve_failure_is_prewrite(); status != 0) return status;
    if (const int status = test_late_write_failure_rolls_back(); status != 0) return status;
    if (const int status = test_rollback_failure_is_explicit(); status != 0) return status;
    if (const int status = test_jump_slot_ignores_original_word(); status != 0) return status;
    if (const int status = test_jump_slot_weak_zero(); status != 0) return status;
    if (const int status = test_jump_slot_resolve_failure_is_prewrite(); status != 0) return status;
    if (const int status = test_plt_late_write_failure_rolls_back(); status != 0) return status;
    if (const int status = test_plt_rollback_failure_is_explicit(); status != 0) return status;
    if (const int status = test_combined_applies_main_then_plt(); status != 0) return status;
    if (const int status = test_combined_plt_prepare_failure_is_prewrite(); status != 0) return status;
    if (const int status = test_combined_duplicate_target_is_prewrite(); status != 0) return status;
    if (const int status = test_combined_plt_write_failure_rolls_back_main(); status != 0) return status;
    if (const int status = test_combined_cross_table_rollback_failure_is_explicit(); status != 0) return status;
    return 0;
}
