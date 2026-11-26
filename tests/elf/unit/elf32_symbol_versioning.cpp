#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32GraphSymbolLookupError;
using liba32android::elf::Elf32HashTableMetadata;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::elf::Elf32StringTableMetadata;
using liba32android::elf::Elf32SymbolLookupError;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::Elf32SymbolTableMetadata;
using liba32android::elf::Elf32VersionSymbolTableMetadata;
using liba32android::elf::Elf32VersionTableMetadata;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::elf::lookup_elf32_graph_symbol_for_reference;
using liba32android::memory::LinearGuestMemory;

constexpr std::uint32_t kMemoryBase = 0x1000;
constexpr std::uint16_t kHidden = 0x8000;
constexpr std::uint32_t kRequesterBase = 0x4000;
constexpr std::uint32_t kProviderBase = 0x8000;
constexpr std::uint32_t kTargetValue = 0x120;
constexpr std::uint32_t kTargetName = 1;
constexpr std::uint32_t kProviderName = 32;
constexpr std::uint32_t kVersionName = 64;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u16(LinearGuestMemory& memory,
               std::uint32_t address,
               std::uint16_t value) {
    const std::array<std::uint8_t, 2> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
    };
    return memory.write(address, bytes);
}

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

bool write_string(LinearGuestMemory& memory,
                  std::uint32_t table,
                  std::uint32_t offset,
                  std::string_view value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    return memory.write(table + offset, bytes);
}

bool write_symbol(LinearGuestMemory& memory,
                  std::uint32_t table,
                  std::uint32_t index,
                  std::uint32_t name_offset,
                  std::uint32_t value,
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
    bytes[12] = 0x12;  // STB_GLOBAL | STT_FUNC
    bytes[13] = 0;
    bytes[14] = static_cast<std::uint8_t>(section_index);
    bytes[15] = static_cast<std::uint8_t>(section_index >> 8U);
    return memory.write(table + index * 16U, bytes);
}

std::uint32_t elf_hash(std::string_view name) {
    std::uint32_t hash = 0;
    for (const unsigned char byte : name) {
        hash = (hash << 4U) + byte;
        const std::uint32_t high = hash & 0xf0000000U;
        if (high != 0) {
            hash ^= high >> 24U;
            hash &= ~high;
        }
    }
    return hash;
}

struct Layout {
    std::uint32_t strings{};
    std::uint32_t symbols{};
    std::uint32_t hash{};
    std::uint32_t versym{};
    std::uint32_t versions{};
};

Layout layout(std::uint32_t base) {
    return Layout{
        .strings = base,
        .symbols = base + 0x400,
        .hash = base + 0x800,
        .versym = base + 0x900,
        .versions = base + 0xa00,
    };
}

bool stage_hash(LinearGuestMemory& memory, const Layout& l) {
    return write_u32(memory, l.hash, 1) &&
           write_u32(memory, l.hash + 4U, 2) &&
           write_u32(memory, l.hash + 8U, 1) &&
           write_u32(memory, l.hash + 12U, 0) &&
           write_u32(memory, l.hash + 16U, 0);
}

void stage_metadata(Elf32LoadedDependencyObject& object,
                    const Layout& l,
                    std::uint32_t load_bias) {
    object.load.load_bias = load_bias;
    object.linker_metadata.string_table =
        Elf32StringTableMetadata{.guest_address = l.strings, .size = 0x200};
    object.linker_metadata.symbol_table =
        Elf32SymbolTableMetadata{.guest_address = l.symbols, .entry_size = 16};
    object.linker_metadata.sysv_hash_table =
        Elf32HashTableMetadata{.guest_address = l.hash};
}

bool stage_graph(LinearGuestMemory& memory, Elf32DependencyGraph& graph) {
    graph.objects.resize(2);
    auto& requester = graph.objects[0];
    auto& provider = graph.objects[1];
    requester.identity = "requester";
    provider.identity = "provider";

    const Layout req = layout(kRequesterBase);
    const Layout def = layout(kProviderBase);
    stage_metadata(requester, req, 0x10000);
    stage_metadata(provider, def, 0x20000);

    if (!write_string(memory, req.strings, kTargetName, "target") ||
        !write_string(memory, req.strings, kProviderName, "libprovider.so") ||
        !write_string(memory, req.strings, kVersionName, "LIBC") ||
        !write_symbol(memory, req.symbols, 1, kTargetName, 0, 0) ||
        !stage_hash(memory, req) ||
        !write_string(memory, def.strings, kTargetName, "target") ||
        !write_string(memory, def.strings, kVersionName, "LIBC") ||
        !write_symbol(memory, def.symbols, 1, kTargetName, kTargetValue, 1) ||
        !stage_hash(memory, def)) {
        return false;
    }

    requester.linker_metadata.has_symbol_versioning = true;
    requester.linker_metadata.version_symbol_table =
        Elf32VersionSymbolTableMetadata{.guest_address = req.versym};
    requester.linker_metadata.version_requirement_table =
        Elf32VersionTableMetadata{.guest_address = req.versions, .count = 1};
    requester.dependencies = {
        Elf32DependencyEdge{
            .requested_name = "libprovider.so",
            .target_object = 1,
        },
    };

    provider.linker_metadata.has_symbol_versioning = true;
    provider.linker_metadata.version_symbol_table =
        Elf32VersionSymbolTableMetadata{.guest_address = def.versym};
    provider.linker_metadata.version_definition_table =
        Elf32VersionTableMetadata{.guest_address = def.versions, .count = 1};
    provider.linker_strings.soname = "libprovider.so";

    if (!write_u16(memory, req.versym + 2U, 2) ||
        !write_u16(memory, def.versym + 2U, 2)) {
        return false;
    }

    // Elf32_Verneed + one Elf32_Vernaux.
    if (!write_u16(memory, req.versions, 1) ||
        !write_u16(memory, req.versions + 2U, 1) ||
        !write_u32(memory, req.versions + 4U, kProviderName) ||
        !write_u32(memory, req.versions + 8U, 16) ||
        !write_u32(memory, req.versions + 12U, 0) ||
        !write_u32(memory, req.versions + 16U, elf_hash("LIBC")) ||
        !write_u16(memory, req.versions + 20U, 0) ||
        !write_u16(memory, req.versions + 22U, 2) ||
        !write_u32(memory, req.versions + 24U, kVersionName) ||
        !write_u32(memory, req.versions + 28U, 0)) {
        return false;
    }

    // Elf32_Verdef + one Elf32_Verdaux.
    if (!write_u16(memory, def.versions, 1) ||
        !write_u16(memory, def.versions + 2U, 0) ||
        !write_u16(memory, def.versions + 4U, 2) ||
        !write_u16(memory, def.versions + 6U, 1) ||
        !write_u32(memory, def.versions + 8U, elf_hash("LIBC")) ||
        !write_u32(memory, def.versions + 12U, 20) ||
        !write_u32(memory, def.versions + 16U, 0) ||
        !write_u32(memory, def.versions + 20U, kVersionName) ||
        !write_u32(memory, def.versions + 24U, 0)) {
        return false;
    }

    return true;
}

Elf32SymbolLookupOptions options(std::uint32_t max_version_records = 16) {
    return Elf32SymbolLookupOptions{
        .max_symbols = 8,
        .max_hash_buckets = 4,
        .max_gnu_bloom_words = 4,
        .max_scope_objects = 4,
        .max_name_bytes = 64,
        .max_version_records = max_version_records,
    };
}

int test_explicit_and_default_version_matching() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    if (!stage_graph(memory, graph)) {
        return fail("could not stage versioned graph");
    }

    const auto explicit_lookup = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", options());
    if (!explicit_lookup ||
        explicit_lookup.symbol.object_index != 1 ||
        explicit_lookup.symbol.symbol.guest_value != 0x20120) {
        return fail("explicit LIBC version did not select provider definition");
    }

    const auto default_lookup =
        lookup_elf32_graph_symbol(memory, graph, 0, "target", options());
    if (!default_lookup || default_lookup.symbol.object_index != 1) {
        return fail("unversioned lookup did not accept non-hidden default definition");
    }

    const Layout def = layout(kProviderBase);
    if (!write_u16(memory, def.versym + 2U,
                   static_cast<std::uint16_t>(kHidden | 2U))) {
        return fail("could not hide provider version");
    }
    const auto hidden_default =
        lookup_elf32_graph_symbol(memory, graph, 0, "target", options());
    if (hidden_default.error != Elf32GraphSymbolLookupError::SymbolNotFound) {
        return fail("unversioned lookup accepted a hidden provider version");
    }
    const auto hidden_explicit = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", options());
    if (!hidden_explicit || hidden_explicit.symbol.object_index != 1) {
        return fail("explicit version request did not match hidden version index");
    }
    return 0;
}

int test_global_fallback_and_failures() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    if (!stage_graph(memory, graph)) {
        return fail("could not stage fallback graph");
    }

    const Layout def = layout(kProviderBase);
    if (!write_string(memory, def.strings, kVersionName, "OTHER") ||
        !write_u16(memory, def.versym + 2U, 1)) {
        return fail("could not stage provider global fallback");
    }
    const auto fallback = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", options());
    if (!fallback || fallback.symbol.object_index != 1) {
        return fail("missing provider VERDEF did not fall back to global version index");
    }

    graph.objects[1].linker_strings.soname = "wrong.so";
    const auto missing_dependency = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", options());
    if (missing_dependency.error !=
            Elf32GraphSymbolLookupError::ObjectLookupFailed ||
        missing_dependency.lookup_error !=
            Elf32SymbolLookupError::VersionDependencyNotFound) {
        return fail("VERNEED dependency mismatch was not rejected");
    }

    graph.objects[1].linker_strings.soname = "libprovider.so";
    const auto limited = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", options(1));
    if (limited.error != Elf32GraphSymbolLookupError::ObjectLookupFailed ||
        limited.lookup_error !=
            Elf32SymbolLookupError::VersionRecordLimitExceeded) {
        return fail("version record ceiling was not enforced");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_explicit_and_default_version_matching();
        status != 0) return status;
    if (const int status = test_global_fallback_and_failures();
        status != 0) return status;
    return 0;
}
