#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "support/fixture_io.h"

#include "elf/elf32_dependency_loader.h"
#include "elf/elf32_relocation.h"
#include "elf/elf32_relro.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyLoadOptions;
using liba32android::elf::Elf32DependencyLoadSource;
using liba32android::elf::Elf32DependencyProvider;
using liba32android::elf::Elf32DependencyProviderError;
using liba32android::elf::Elf32DependencyProviderResult;
using liba32android::elf::Elf32GraphSymbolLookupResult;
using liba32android::elf::Elf32LoadedSegment;
using liba32android::elf::Elf32RelocationOptions;
using liba32android::elf::Elf32RelroOptions;
using liba32android::elf::Elf32RelroSegment;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::apply_elf32_plt_rel_relocations;
using liba32android::elf::apply_elf32_rel_relocations;
using liba32android::elf::load_elf32_dependency_graph;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::elf::seal_elf32_gnu_relro;
using liba32android::memory::MappedGuestMemory;
using liba32android::memory::MemoryPermission;

constexpr std::uint32_t kMaxFixtureNameBytes = 128;
constexpr std::uint64_t kMaxFixtureImageBytes = 4U << 20;
constexpr std::uint64_t kMaxTotalImageBytes = 8U << 20;
constexpr std::uint32_t kBssRelOffset = 0x82cc;
constexpr std::uint32_t kDataRelOffset = 0x82d0;

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
    return std::string("real ARM32 RELRO fixture symbol lookup failed for ") +
           std::string(name) + ": graph=" +
           liba32android::elf::to_string(result.error) + ", index=" +
           liba32android::elf::to_string(result.index_error) + ", lookup=" +
           liba32android::elf::to_string(result.lookup_error) + ", string=" +
           liba32android::elf::to_string(result.string_error);
}

struct SegmentSnapshot {
    std::uint32_t guest_address{};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_size{};
    std::vector<std::uint8_t> bytes;
};

struct PageSnapshot {
    std::uint32_t address{};
    MemoryPermission permissions{MemoryPermission::None};
};

bool snapshot_after_relocation(
    const MappedGuestMemory& memory,
    const std::vector<Elf32LoadedSegment>& segments,
    std::vector<SegmentSnapshot>& byte_snapshots,
    std::vector<PageSnapshot>& page_snapshots) {
    byte_snapshots.clear();
    page_snapshots.clear();

    const std::uint64_t page_size = memory.page_size();
    for (const auto& segment : segments) {
        SegmentSnapshot snapshot;
        snapshot.guest_address = segment.guest_address;
        snapshot.mapping_start = segment.mapping_start;
        snapshot.mapping_size = segment.mapping_size;
        snapshot.bytes.resize(segment.memory_size);
        if (!memory.read(segment.guest_address, snapshot.bytes)) {
            return false;
        }
        byte_snapshots.push_back(std::move(snapshot));

        const std::uint64_t mapping_end =
            static_cast<std::uint64_t>(segment.mapping_start) +
            segment.mapping_size;
        for (std::uint64_t page = segment.mapping_start;
             page < mapping_end;
             page += page_size) {
            if (page > std::numeric_limits<std::uint32_t>::max()) return false;
            page_snapshots.push_back({
                .address = static_cast<std::uint32_t>(page),
                .permissions =
                    memory.permissions(static_cast<std::uint32_t>(page)),
            });
        }
    }
    return true;
}

bool page_is_relro(
    std::uint32_t page,
    const std::vector<Elf32RelroSegment>& relro_segments) {
    for (const auto& relro : relro_segments) {
        const std::uint64_t start = relro.mapping_start;
        const std::uint64_t end = start + relro.mapping_size;
        if (page >= start && page < end) return true;
    }
    return false;
}

bool bytes_unchanged(
    const MappedGuestMemory& memory,
    const std::vector<SegmentSnapshot>& snapshots) {
    for (const auto& snapshot : snapshots) {
        std::vector<std::uint8_t> after(snapshot.bytes.size());
        if (!memory.read(snapshot.guest_address, after) ||
            after != snapshot.bytes) {
            return false;
        }
    }
    return true;
}

bool permissions_match_relro_result(
    const MappedGuestMemory& memory,
    const std::vector<PageSnapshot>& before,
    const std::vector<Elf32RelroSegment>& relro_segments) {
    for (const auto& page : before) {
        const MemoryPermission after = memory.permissions(page.address);
        if (page_is_relro(page.address, relro_segments)) {
            if (after != MemoryPermission::Read) return false;
        } else if (after != page.permissions) {
            return false;
        }
    }
    return true;
}

bool address_in_exact_relro(
    std::uint32_t address,
    const std::vector<Elf32RelroSegment>& relro_segments) {
    for (const auto& relro : relro_segments) {
        const std::uint64_t start = relro.guest_address;
        const std::uint64_t end = start + relro.memory_size;
        if (address >= start &&
            static_cast<std::uint64_t>(address) + 4U <= end) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return fail("expected path to generated ARM32 fixture");
    }

    const std::vector<std::uint8_t> image = liba32android::test_support::read_binary_file(argv[1]);
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
            .identity = "real-arm32-relro-fixture",
            .image = image,
        },
        provider,
        load_options);
    if (!graph_result) {
        return fail(
            std::string("real ARM32 RELRO dependency graph load failed: ") +
            liba32android::elf::to_string(graph_result.error));
    }
    if (graph_result.graph.objects.size() != 1 || provider.calls != 0) {
        return fail("real RELRO fixture did not remain a one-object zero-dependency graph");
    }

    const auto& root = graph_result.graph.objects.front();
    if (root.load.relro_segments.size() != 1) {
        return fail("real RELRO fixture did not expose exactly one GNU RELRO range");
    }

    const auto lookup_options = symbol_options();
    const auto data = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_data", lookup_options);
    const auto bss = lookup_elf32_graph_symbol(
        memory, graph_result.graph, 0, "fixture_bss", lookup_options);
    if (!data) return fail(lookup_error("fixture_data", data));
    if (!bss) return fail(lookup_error("fixture_bss", bss));

    const std::uint32_t bss_target = root.load.load_bias + kBssRelOffset;
    const std::uint32_t data_target = root.load.load_bias + kDataRelOffset;
    if (!address_in_exact_relro(bss_target, root.load.relro_segments) ||
        !address_in_exact_relro(data_target, root.load.relro_segments)) {
        return fail("real GLOB_DAT targets were not contained in GNU RELRO");
    }

    const auto main_applied = apply_elf32_rel_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!main_applied || main_applied.application.writes.size() != 2) {
        return fail(
            std::string("real RELRO fixture main relocation application failed: ") +
            liba32android::elf::to_string(main_applied.error));
    }

    const auto plt_applied = apply_elf32_plt_rel_relocations(
        memory, graph_result.graph, 0, relocation_options());
    if (!plt_applied || !plt_applied.application.writes.empty()) {
        return fail("zero-PLT real fixture did not complete the eager PLT path as empty work");
    }

    std::uint32_t bss_before_seal = 0;
    std::uint32_t data_before_seal = 0;
    if (!liba32android::test_support::read_u32_le(memory, bss_target, bss_before_seal) ||
        !liba32android::test_support::read_u32_le(memory, data_target, data_before_seal) ||
        bss_before_seal != bss.symbol.symbol.guest_value ||
        data_before_seal != data.symbol.symbol.guest_value) {
        return fail("real relocation target values were incorrect before RELRO sealing");
    }

    const auto& relro = root.load.relro_segments.front();
    bool writable_before_seal = false;
    const std::uint64_t relro_end =
        static_cast<std::uint64_t>(relro.mapping_start) + relro.mapping_size;
    for (std::uint64_t page = relro.mapping_start;
         page < relro_end;
         page += memory.page_size()) {
        if (page > std::numeric_limits<std::uint32_t>::max()) {
            return fail("real RELRO page address overflowed");
        }
        const MemoryPermission permissions =
            memory.permissions(static_cast<std::uint32_t>(page));
        writable_before_seal =
            writable_before_seal ||
            liba32android::memory::has_permission(
                permissions, MemoryPermission::Write);
    }
    if (!writable_before_seal) {
        return fail("real GNU RELRO was not writable before the explicit seal");
    }

    std::vector<SegmentSnapshot> bytes_before_seal;
    std::vector<PageSnapshot> permissions_before_seal;
    if (!snapshot_after_relocation(
            memory,
            root.load.segments,
            bytes_before_seal,
            permissions_before_seal)) {
        return fail("could not snapshot relocated fixture before RELRO sealing");
    }

    const auto sealed = seal_elf32_gnu_relro(
        memory,
        root.load,
        Elf32RelroOptions{.max_pages = 8});
    if (!sealed || sealed.sealed_pages == 0) {
        return fail(
            std::string("real GNU RELRO sealing failed: ") +
            liba32android::elf::to_string(sealed.error));
    }

    std::uint32_t bss_after_seal = 0;
    std::uint32_t data_after_seal = 0;
    if (!liba32android::test_support::read_u32_le(memory, bss_target, bss_after_seal) ||
        !liba32android::test_support::read_u32_le(memory, data_target, data_after_seal) ||
        bss_after_seal != bss_before_seal ||
        data_after_seal != data_before_seal) {
        return fail("RELRO sealing changed already-applied relocation target bytes");
    }

    constexpr std::array<std::uint8_t, 1> byte{0xaa};
    if (memory.write(bss_target, byte) || memory.write(data_target, byte)) {
        return fail("write unexpectedly succeeded inside sealed GNU RELRO");
    }

    if (!bytes_unchanged(memory, bytes_before_seal)) {
        return fail("GNU RELRO sealing changed loaded guest bytes");
    }
    if (!permissions_match_relro_result(
            memory, permissions_before_seal, root.load.relro_segments)) {
        return fail("GNU RELRO sealing changed non-RELRO permissions or failed to make RELRO read-only");
    }
    if (provider.calls != 0) {
        return fail("RELRO flow unexpectedly called the dependency provider");
    }

    std::cout << "fixture.relro.segment_count="
              << root.load.relro_segments.size() << '\n'
              << "fixture.relro.sealed_pages=" << sealed.sealed_pages << '\n'
              << "fixture.relro.bss_target=0x" << std::hex << bss_target << '\n'
              << "fixture.relro.bss_value=0x" << bss_after_seal << '\n'
              << "fixture.relro.data_target=0x" << data_target << '\n'
              << "fixture.relro.data_value=0x" << data_after_seal << std::dec << '\n'
              << "fixture.relro.writable_before_seal=true\n"
              << "fixture.relro.write_rejected_after_seal=true\n"
              << "fixture.relro.relocated_bytes_unchanged=true\n"
              << "fixture.relro.non_relro_permissions_unchanged=true\n"
              << "fixture.relro.provider_calls=" << provider.calls << '\n'
              << "fixture.relro.status=PASS\n";
    return 0;
}
