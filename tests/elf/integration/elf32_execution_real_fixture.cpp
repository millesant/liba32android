#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
#include "elf/elf32_relro.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::cpu::ExecutionRequest;
using liba32android::cpu::InstructionSet;
using liba32android::cpu::execute;
using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32RelroOptions;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_combined_relocations;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::elf::seal_elf32_gnu_relro;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::uint32_t kFixtureDataValue = 0x12345678U;
constexpr std::uint32_t kFixtureBssValue = 7U;
constexpr std::uint32_t kArg0 = 5U;
constexpr std::uint32_t kArg1 = 6U;
constexpr std::uint32_t kReturnMarker = 0x5aU;
constexpr std::size_t kInstructionBudget = 256;
constexpr std::size_t kStackPages = 4;

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

class FailIfCalledProvider final : public Elf32DependencyProvider {
public:
    std::size_t calls{};

    Elf32DependencyProviderResult resolve(
        std::string_view,
        std::uint64_t) override {
        ++calls;
        Elf32DependencyProviderResult result;
        result.error = Elf32DependencyProviderError::Failed;
        return result;
    }
};

Elf32SymbolLookupOptions symbol_options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 256,
        .max_hash_buckets = 256,
        .max_gnu_bloom_words = 64,
        .max_scope_objects = 8,
        .max_name_bytes = kMaxFixtureNameBytes,
    };
}

Elf32RelocationOptions relocation_options() {
    Elf32RelocationOptions result;
    result.max_relocations = 16;
    result.symbols = symbol_options();
    return result;
}

std::string lookup_error(
    std::string_view name,
    const Elf32GraphSymbolLookupResult& result) {
    return std::string("real ARM32 execution fixture symbol lookup failed for ") +
           std::string(name) + ": graph=" +
           liba32android::elf::to_string(result.error) + ", index=" +
           liba32android::elf::to_string(result.index_error) + ", lookup=" +
           liba32android::elf::to_string(result.lookup_error) + ", string=" +
           liba32android::elf::to_string(result.string_error);
}

bool write_u32(
    MappedGuestMemory& memory,
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

std::optional<std::uint32_t> find_unmapped_region(
    const MappedGuestMemory& memory,
    std::uint32_t start,
    std::size_t page_count) {
    const std::uint64_t page_size = memory.page_size();
    const std::uint64_t length = page_size * page_count;
    if (page_count == 0 || length > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    for (std::uint64_t candidate = start;
         candidate + length <= 0xf0000000ULL;
         candidate += page_size * 16U) {
        bool available = true;
        for (std::size_t i = 0; i < page_count; ++i) {
            const std::uint64_t page = candidate + i * page_size;
            if (page > std::numeric_limits<std::uint32_t>::max() ||
                memory.is_mapped(static_cast<std::uint32_t>(page))) {
                available = false;
                break;
            }
        }
        if (available) return static_cast<std::uint32_t>(candidate);
    }
    return std::nullopt;
}

bool prepare_return_sentinel(
    MappedGuestMemory& memory,
    std::uint32_t address,
    InstructionSet instruction_set) {
    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    if (!memory.map(address, memory.page_size(), rw)) return false;

    if (instruction_set == InstructionSet::Thumb) {
        // movs r7,#0x5a ; b.n .
        constexpr std::array<std::uint8_t, 4> code{
            0x5a, 0x27, 0xfe, 0xe7,
        };
        if (!memory.write(address, code)) return false;
    } else {
        // mov r7,#0x5a ; b .
        constexpr std::array<std::uint8_t, 8> code{
            0x5a, 0x70, 0xa0, 0xe3,
            0xfe, 0xff, 0xff, 0xea,
        };
        if (!memory.write(address, code)) return false;
    }
    return memory.protect(address, memory.page_size(), rx);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected path to generated ARM32 fixture");
    }

    const std::vector<std::uint8_t> image =
        liba32android::test_support::read_binary_file(argv[1]);
    if (image.empty()) {
        return fail("generated ARM32 fixture is missing or empty");
    }

    MappedGuestMemory memory;
    FailIfCalledProvider provider;

    Elf32DependencyLoadOptions load_options;
    load_options.max_objects = 8;
    load_options.max_depth = 8;
    load_options.max_dependency_occurrences = 8;
    load_options.max_image_bytes = kMaxFixtureImageBytes;
    load_options.max_total_image_bytes = kMaxTotalImageBytes;
    load_options.max_string_bytes = kMaxFixtureNameBytes;

    const auto graph_result = load_elf32_dependency_graph(
        memory,
        Elf32DependencyLoadSource{
            .identity = "real-arm32-execution-fixture",
            .image = image,
        },
        provider,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("real ARM32 execution dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 1 || provider.calls != 0) {
        return fail("real execution fixture did not remain a one-object zero-dependency graph");
    }

    const auto lookup_options = symbol_options();
    const auto add = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_add", lookup_options);
    const auto data = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_data", lookup_options);
    const auto bss = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_bss", lookup_options);
    if (!add) return fail(lookup_error("fixture_add", add));
    if (!data) return fail(lookup_error("fixture_data", data));
    if (!bss) return fail(lookup_error("fixture_bss", bss));
    if (add.symbol.object_index != 0 ||
        data.symbol.object_index != 0 ||
        bss.symbol.object_index != 0) {
        return fail("real execution fixture symbols resolved outside root object");
    }

    const auto& add_symbol = add.symbol.symbol;
    if (add_symbol.symbol.type != 2U || add_symbol.symbol.size == 0) {
        return fail("fixture_add was not a non-empty STT_FUNC");
    }
    const bool thumb = (add_symbol.symbol.value & 1U) != 0;
    const InstructionSet instruction_set =
        thumb ? InstructionSet::Thumb : InstructionSet::Arm;
    const std::uint32_t entry_pc = add_symbol.guest_value & ~1U;

    const auto relocated = apply_elf32_combined_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!relocated || relocated.application.writes.size() != 2) {
        return fail(
            std::string("real execution fixture relocation failed: ") +
            liba32android::elf::to_string(relocated.error));
    }

    if (!write_u32(memory, bss.symbol.symbol.guest_value, kFixtureBssValue)) {
        return fail("could not initialize fixture_bss before execution");
    }

    const auto& root = graph_result.graph.objects.front();
    const auto sealed = seal_elf32_gnu_relro(
        memory,
        root.load,
        Elf32RelroOptions{.max_pages = 8});
    if (!sealed || sealed.sealed_pages == 0) {
        return fail(
            std::string("real execution fixture RELRO sealing failed: ") +
            liba32android::elf::to_string(sealed.error));
    }

    std::uint32_t data_value = 0;
    std::uint32_t bss_value = 0;
    if (!liba32android::test_support::read_u32_le(
            memory, data.symbol.symbol.guest_value, data_value) ||
        !liba32android::test_support::read_u32_le(
            memory, bss.symbol.symbol.guest_value, bss_value) ||
        data_value != kFixtureDataValue ||
        bss_value != kFixtureBssValue) {
        return fail("fixture data/BSS values were incorrect before execution");
    }

    const auto sentinel =
        find_unmapped_region(memory, 0x70000000U, 1);
    const auto stack =
        find_unmapped_region(memory, 0x71000000U, kStackPages);
    if (!sentinel.has_value() || !stack.has_value() ||
        *sentinel == *stack) {
        return fail("could not reserve deterministic execution harness regions");
    }
    if (!prepare_return_sentinel(memory, *sentinel, instruction_set)) {
        return fail("could not prepare execution return sentinel");
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const std::size_t stack_size = memory.page_size() * kStackPages;
    if (!memory.map(*stack, stack_size, rw)) {
        return fail("could not map execution stack");
    }
    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(*stack) + stack_size - 16U;
    if (stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return fail("execution stack top overflowed guest address space");
    }

    ExecutionRequest request{};
    request.instruction_set = instruction_set;
    request.entry_pc = entry_pc;
    request.regs[0] = kArg0;
    request.regs[1] = kArg1;
    request.regs[7] = 0;
    request.regs[13] = static_cast<std::uint32_t>(stack_top64) & ~7U;
    request.regs[14] = *sentinel | (thumb ? 1U : 0U);
    request.instruction_count = kInstructionBudget;

    const auto result = execute(memory, request);
    const std::uint32_t expected =
        kFixtureDataValue + kFixtureBssValue + kArg0 + kArg1;

    std::uint32_t bss_after = 0;
    if (result.exception_raised ||
        result.memory_fault ||
        result.instructions_executed != kInstructionBudget ||
        result.regs[0] != expected ||
        result.regs[7] != kReturnMarker ||
        !result.fastmem_enabled ||
        !liba32android::test_support::read_u32_le(
            memory, bss.symbol.symbol.guest_value, bss_after) ||
        bss_after != kFixtureBssValue) {
        return fail("real ARM32 fixture function did not execute and return through the sentinel");
    }
    if (provider.calls != 0) {
        return fail("real execution unexpectedly called the dependency provider");
    }

    std::cout << "fixture.execution.mode="
              << (thumb ? "thumb" : "arm") << '\n'
              << "fixture.execution.entry_pc=0x" << std::hex << entry_pc << '\n'
              << "fixture.execution.relocation_count=" << std::dec
              << relocated.application.writes.size() << '\n'
              << "fixture.execution.relro_pages=" << sealed.sealed_pages << '\n'
              << "fixture.execution.result=0x" << std::hex << result.regs[0] << '\n'
              << "fixture.execution.expected=0x" << expected << '\n'
              << "fixture.execution.return_marker=0x" << result.regs[7] << std::dec << '\n'
              << "fixture.execution.instructions=" << result.instructions_executed << '\n'
              << "fixture.execution.fastmem_enabled=true\n"
              << "fixture.execution.provider_calls=" << provider.calls << '\n'
              << "fixture.execution.status=PASS\n";
    return 0;
}
