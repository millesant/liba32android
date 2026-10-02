#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "elf/elf32_link_map.h"
#include "elf/elf32_symbol_lookup.h"
#include "memory/guest_memory.h"

namespace {

using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32GraphSymbolLookupError;
using liba32android::elf::Elf32HashTableMetadata;
using liba32android::elf::Elf32LinkerMetadata;
using liba32android::elf::Elf32LoadedDependencyObject;
using liba32android::elf::Elf32LinkMap;
using liba32android::elf::Elf32StringTableMetadata;
using liba32android::elf::Elf32ObjectSymbolLookupResult;
using liba32android::elf::Elf32SymbolIndexError;
using liba32android::elf::Elf32SymbolLookupError;
using liba32android::elf::Elf32SymbolLookupOptions;
using liba32android::elf::Elf32SymbolTableMetadata;
using liba32android::elf::build_elf32_symbol_index;
using liba32android::elf::lookup_elf32_graph_symbol;
using liba32android::elf::lookup_elf32_graph_symbol_for_reference;
using liba32android::elf::lookup_elf32_symbol;
using liba32android::memory::GuestMemory;
using liba32android::memory::LinearGuestMemory;

constexpr std::uint32_t kMemoryBase = 0x1000;
constexpr std::uint32_t kStringTable = 0x1100;
constexpr std::uint32_t kSymbolTable = 0x1800;
constexpr std::uint32_t kSysvHash = 0x2800;
constexpr std::uint32_t kGnuHash = 0x3800;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

class RecordingGuestMemory final : public GuestMemory {
public:
    struct ReadEvent {
        std::uint32_t address{};
        std::size_t size{};
    };

    RecordingGuestMemory(std::size_t size, std::uint32_t base)
        : backing_{size, base} {}

    bool read(
        std::uint32_t address,
        std::span<std::uint8_t> output) const override {
        reads_.push_back(ReadEvent{
            .address = address,
            .size = output.size(),
        });
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

    void reset_reads() const {
        reads_.clear();
    }

    [[nodiscard]] std::size_t count_reads(
        std::uint32_t address,
        std::size_t size) const {
        return static_cast<std::size_t>(std::count_if(
            reads_.begin(),
            reads_.end(),
            [&](const ReadEvent& event) {
                return event.address == address && event.size == size;
            }));
    }

private:
    LinearGuestMemory backing_;
    mutable std::vector<ReadEvent> reads_;
};

Elf32SymbolLookupOptions options() {
    return Elf32SymbolLookupOptions{
        .max_symbols = 16,
        .max_hash_buckets = 8,
        .max_gnu_bloom_words = 8,
        .max_scope_objects = 16,
        .max_name_bytes = 64,
    };
}

Elf32LinkerMetadata metadata(bool sysv, bool gnu) {
    Elf32LinkerMetadata result;
    result.string_table = Elf32StringTableMetadata{
        .guest_address = kStringTable,
        .size = 0x100,
    };
    result.symbol_table = Elf32SymbolTableMetadata{
        .guest_address = kSymbolTable,
        .entry_size = 16,
    };
    if (sysv) {
        result.sysv_hash_table =
            Elf32HashTableMetadata{.guest_address = kSysvHash};
    }
    if (gnu) {
        result.gnu_hash_table =
            Elf32HashTableMetadata{.guest_address = kGnuHash};
    }
    return result;
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

bool stage_sysv(LinearGuestMemory& memory,
                std::uint32_t bucket_count,
                std::uint32_t chain_count,
                std::array<std::uint32_t, 2> buckets,
                std::array<std::uint32_t, 4> chains) {
    if (!write_u32(memory, kSysvHash, bucket_count) ||
        !write_u32(memory, kSysvHash + 4U, chain_count)) {
        return false;
    }
    for (std::uint32_t i = 0; i < buckets.size(); ++i) {
        if (!write_u32(memory, kSysvHash + 8U + i * 4U, buckets[i])) {
            return false;
        }
    }
    const std::uint32_t chains_address =
        kSysvHash + 8U + bucket_count * 4U;
    for (std::uint32_t i = 0; i < chains.size(); ++i) {
        if (!write_u32(memory, chains_address + i * 4U, chains[i])) {
            return false;
        }
    }
    return true;
}

bool stage_gnu(LinearGuestMemory& memory,
               std::uint32_t bucket_count,
               std::uint32_t symbol_offset,
               std::uint32_t bloom_words,
               std::uint32_t bloom_shift,
               std::array<std::uint32_t, 2> buckets,
               std::array<std::uint32_t, 4> chains) {
    if (!write_u32(memory, kGnuHash, bucket_count) ||
        !write_u32(memory, kGnuHash + 4U, symbol_offset) ||
        !write_u32(memory, kGnuHash + 8U, bloom_words) ||
        !write_u32(memory, kGnuHash + 12U, bloom_shift)) {
        return false;
    }
    for (std::uint32_t i = 0; i < bloom_words; ++i) {
        if (!write_u32(memory, kGnuHash + 16U + i * 4U, 0)) {
            return false;
        }
    }
    const std::uint32_t buckets_address =
        kGnuHash + 16U + bloom_words * 4U;
    for (std::uint32_t i = 0; i < buckets.size(); ++i) {
        if (!write_u32(memory, buckets_address + i * 4U, buckets[i])) {
            return false;
        }
    }
    const std::uint32_t chains_address =
        buckets_address + bucket_count * 4U;
    for (std::uint32_t i = 0; i < chains.size(); ++i) {
        if (!write_u32(memory, chains_address + i * 4U, chains[i])) {
            return false;
        }
    }
    return true;
}

int test_required_inputs_and_options() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    auto valid = options();

    if (build_elf32_symbol_index(memory, metadata(true, false), {})
            .error != Elf32SymbolIndexError::InvalidOptions) {
        return fail("zero index limits were not rejected");
    }

    Elf32LinkerMetadata missing;
    if (build_elf32_symbol_index(memory, missing, valid).error !=
        Elf32SymbolIndexError::MissingStringTable) {
        return fail("missing STRTAB was not rejected");
    }
    missing.string_table =
        Elf32StringTableMetadata{.guest_address = kStringTable, .size = 16};
    if (build_elf32_symbol_index(memory, missing, valid).error !=
        Elf32SymbolIndexError::MissingSymbolTable) {
        return fail("missing SYMTAB was not rejected");
    }
    missing.symbol_table =
        Elf32SymbolTableMetadata{.guest_address = kSymbolTable,
                                 .entry_size = 16};
    if (build_elf32_symbol_index(memory, missing, valid).error !=
        Elf32SymbolIndexError::MissingHashTable) {
        return fail("missing dynamic hash was not rejected");
    }
    return 0;
}

int test_valid_sysv_index() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    if (!stage_sysv(memory, 2, 4, {1, 0}, {0, 2, 3, 0})) {
        return fail("could not stage valid SysV hash");
    }

    const auto result =
        build_elf32_symbol_index(memory, metadata(true, false), options());
    if (!result || result.index.symbol_count != 4 ||
        !result.index.sysv_hash.has_value() ||
        result.index.gnu_hash.has_value() ||
        result.index.sysv_hash->bucket_count != 2 ||
        result.index.sysv_hash->chain_count != 4 ||
        result.index.sysv_hash->buckets_guest_address != kSysvHash + 8U ||
        result.index.sysv_hash->chains_guest_address != kSysvHash + 16U) {
        return fail("valid SysV hash did not produce the expected symbol index");
    }
    return 0;
}

int test_sysv_failures() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_sysv(memory, 0, 4, {0, 0}, {0, 0, 0, 0})) {
            return fail("could not stage zero-bucket SysV hash");
        }
        if (build_elf32_symbol_index(memory, metadata(true, false), options())
                .error != Elf32SymbolIndexError::InvalidSysvHash) {
            return fail("zero SysV bucket count was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kSysvHash, 9) ||
            !write_u32(memory, kSysvHash + 4U, 4)) {
            return fail("could not stage oversized SysV bucket count");
        }
        if (build_elf32_symbol_index(memory, metadata(true, false), options())
                .error != Elf32SymbolIndexError::HashLimitExceeded) {
            return fail("SysV bucket ceiling was not enforced");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kSysvHash, 1) ||
            !write_u32(memory, kSysvHash + 4U, 17)) {
            return fail("could not stage oversized SysV symbol count");
        }
        if (build_elf32_symbol_index(memory, metadata(true, false), options())
                .error != Elf32SymbolIndexError::SymbolCountExceeded) {
            return fail("SysV nchain symbol ceiling was not enforced");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_sysv(memory, 2, 4, {4, 0}, {0, 0, 0, 0})) {
            return fail("could not stage bad SysV bucket index");
        }
        if (build_elf32_symbol_index(memory, metadata(true, false), options())
                .error != Elf32SymbolIndexError::HashIndexOutOfRange) {
            return fail("out-of-range SysV bucket index was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_sysv(memory, 2, 4, {1, 0}, {0, 2, 4, 0})) {
            return fail("could not stage bad SysV chain index");
        }
        if (build_elf32_symbol_index(memory, metadata(true, false), options())
                .error != Elf32SymbolIndexError::HashIndexOutOfRange) {
            return fail("out-of-range SysV chain index was not rejected");
        }
    }
    return 0;
}

int test_valid_gnu_index() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    if (!stage_gnu(memory, 2, 1, 1, 5, {1, 3},
                   {0x100, 0x101, 0x201, 0})) {
        return fail("could not stage valid GNU hash");
    }

    const auto result =
        build_elf32_symbol_index(memory, metadata(false, true), options());
    if (!result || result.index.symbol_count != 4 ||
        result.index.sysv_hash.has_value() ||
        !result.index.gnu_hash.has_value() ||
        result.index.gnu_hash->bucket_count != 2 ||
        result.index.gnu_hash->symbol_offset != 1 ||
        result.index.gnu_hash->bloom_word_count != 1 ||
        result.index.gnu_hash->bloom_shift != 5 ||
        result.index.gnu_hash->bloom_guest_address != kGnuHash + 16U ||
        result.index.gnu_hash->buckets_guest_address != kGnuHash + 20U ||
        result.index.gnu_hash->chains_guest_address != kGnuHash + 28U) {
        return fail("valid GNU hash did not produce the expected symbol index");
    }

    LinearGuestMemory empty_memory(0x6000, kMemoryBase);
    if (!stage_gnu(empty_memory, 2, 1, 1, 5, {0, 0},
                   {0, 0, 0, 0})) {
        return fail("could not stage empty GNU buckets");
    }
    const auto empty =
        build_elf32_symbol_index(empty_memory, metadata(false, true), options());
    if (!empty || empty.index.symbol_count != 1) {
        return fail("all-zero GNU buckets did not preserve symoffset count");
    }
    return 0;
}

int test_gnu_failures() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_gnu(memory, 1, 1, 3, 5, {1, 0},
                       {1, 0, 0, 0})) {
            return fail("could not stage non-power-of-two GNU bloom count");
        }
        if (build_elf32_symbol_index(memory, metadata(false, true), options())
                .error != Elf32SymbolIndexError::InvalidGnuHash) {
            return fail("non-power-of-two GNU bloom count was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kGnuHash, 1) ||
            !write_u32(memory, kGnuHash + 4U, 1) ||
            !write_u32(memory, kGnuHash + 8U, 1) ||
            !write_u32(memory, kGnuHash + 12U, 32)) {
            return fail("could not stage invalid GNU bloom shift");
        }
        if (build_elf32_symbol_index(memory, metadata(false, true), options())
                .error != Elf32SymbolIndexError::InvalidGnuHash) {
            return fail("out-of-range GNU bloom shift was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kGnuHash, 9) ||
            !write_u32(memory, kGnuHash + 4U, 1) ||
            !write_u32(memory, kGnuHash + 8U, 1) ||
            !write_u32(memory, kGnuHash + 12U, 5)) {
            return fail("could not stage oversized GNU bucket count");
        }
        if (build_elf32_symbol_index(memory, metadata(false, true), options())
                .error != Elf32SymbolIndexError::HashLimitExceeded) {
            return fail("GNU bucket ceiling was not enforced");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_gnu(memory, 1, 2, 1, 5, {1, 0},
                       {1, 0, 0, 0})) {
            return fail("could not stage GNU bucket below symoffset");
        }
        if (build_elf32_symbol_index(memory, metadata(false, true), options())
                .error != Elf32SymbolIndexError::InvalidGnuHash) {
            return fail("GNU bucket below symoffset was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_gnu(memory, 1, 1, 1, 5, {16, 0},
                       {1, 0, 0, 0})) {
            return fail("could not stage oversized GNU symbol index");
        }
        if (build_elf32_symbol_index(memory, metadata(false, true), options())
                .error != Elf32SymbolIndexError::SymbolCountExceeded) {
            return fail("GNU symbol ceiling was not enforced");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!stage_gnu(memory, 1, 1, 1, 5, {2, 0},
                       {0, 0x100, 0x100, 0})) {
            return fail("could not stage unterminated GNU chain");
        }
        auto limited = options();
        limited.max_symbols = 4;
        if (build_elf32_symbol_index(memory, metadata(false, true), limited)
                .error != Elf32SymbolIndexError::UnterminatedGnuChain) {
            return fail("unterminated GNU-only chain was not rejected");
        }
    }
    return 0;
}

int test_dual_hash_consistency() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kSysvHash, 1) ||
            !write_u32(memory, kSysvHash + 4U, 5) ||
            !write_u32(memory, kSysvHash + 8U, 1)) {
            return fail("could not stage dual-hash SysV header");
        }
        for (std::uint32_t i = 0; i < 5; ++i) {
            if (!write_u32(memory, kSysvHash + 12U + i * 4U, 0)) {
                return fail("could not stage dual-hash SysV chains");
            }
        }
        if (!stage_gnu(memory, 1, 1, 1, 5, {4, 0},
                       {0, 0, 0, 1})) {
            return fail("could not stage dual-hash GNU table");
        }

        const auto result =
            build_elf32_symbol_index(memory, metadata(true, true), options());
        if (!result || result.index.symbol_count != 5 ||
            !result.index.sysv_hash.has_value() ||
            !result.index.gnu_hash.has_value()) {
            return fail("valid dual hash did not retain SysV symbol extent");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kSysvHash, 1) ||
            !write_u32(memory, kSysvHash + 4U, 5) ||
            !write_u32(memory, kSysvHash + 8U, 1)) {
            return fail("could not stage dual-hash bound header");
        }
        for (std::uint32_t i = 0; i < 5; ++i) {
            if (!write_u32(memory, kSysvHash + 12U + i * 4U, 0)) {
                return fail("could not stage dual-hash bound chains");
            }
        }
        if (!stage_gnu(memory, 1, 1, 1, 5, {5, 0},
                       {0, 0, 0, 1})) {
            return fail("could not stage out-of-range GNU bucket");
        }
        if (build_elf32_symbol_index(memory, metadata(true, true), options())
                .error != Elf32SymbolIndexError::HashIndexOutOfRange) {
            return fail("GNU bucket outside SysV nchain was not rejected");
        }
    }
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_u32(memory, kSysvHash, 1) ||
            !write_u32(memory, kSysvHash + 4U, 5) ||
            !write_u32(memory, kSysvHash + 8U, 1)) {
            return fail("could not stage unterminated dual SysV header");
        }
        for (std::uint32_t i = 0; i < 5; ++i) {
            if (!write_u32(memory, kSysvHash + 12U + i * 4U, 0)) {
                return fail("could not stage unterminated dual SysV chains");
            }
        }
        if (!stage_gnu(memory, 1, 1, 1, 5, {4, 0},
                       {0, 0, 0, 0})) {
            return fail("could not stage unterminated dual GNU chain");
        }
        if (build_elf32_symbol_index(memory, metadata(true, true), options())
                .error != Elf32SymbolIndexError::UnterminatedGnuChain) {
            return fail("unterminated GNU chain with SysV count was not rejected");
        }
    }
    return 0;
}

int test_read_failures_and_symbol_range() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        auto bad = metadata(true, false);
        bad.sysv_hash_table =
            Elf32HashTableMetadata{.guest_address = 0x8000};
        if (build_elf32_symbol_index(memory, bad, options()).error !=
            Elf32SymbolIndexError::HashHeaderReadFailed) {
            return fail("unreadable hash header was not classified");
        }
    }
    {
        LinearGuestMemory memory(0x1000, kMemoryBase);
        Elf32LinkerMetadata bad;
        bad.string_table =
            Elf32StringTableMetadata{.guest_address = 0x1100, .size = 0x20};
        bad.symbol_table =
            Elf32SymbolTableMetadata{.guest_address = 0x1ff0,
                                     .entry_size = 16};
        bad.sysv_hash_table =
            Elf32HashTableMetadata{.guest_address = 0x1200};
        if (!write_u32(memory, 0x1200, 1) ||
            !write_u32(memory, 0x1204, 2) ||
            !write_u32(memory, 0x1208, 1) ||
            !write_u32(memory, 0x120c, 0) ||
            !write_u32(memory, 0x1210, 0)) {
            return fail("could not stage symbol-range failure table");
        }
        if (build_elf32_symbol_index(memory, bad, options()).error !=
            Elf32SymbolIndexError::SymbolReadFailed) {
            return fail("unreadable full symbol range was not rejected");
        }
    }
    return 0;
}

bool write_string(LinearGuestMemory& memory,
                  std::uint32_t offset,
                  std::string_view value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    return memory.write(kStringTable + offset, bytes);
}

bool write_symbol(LinearGuestMemory& memory,
                  std::uint32_t index,
                  std::uint32_t name_offset,
                  std::uint32_t value,
                  std::uint32_t size,
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
    put_u32(8, size);
    bytes[12] = static_cast<std::uint8_t>((binding << 4U) | (type & 0x0fU));
    bytes[13] = other;
    bytes[14] = static_cast<std::uint8_t>(section_index);
    bytes[15] = static_cast<std::uint8_t>(section_index >> 8U);
    return memory.write(kSymbolTable + index * 16U, bytes);
}

bool stage_sysv_lookup(LinearGuestMemory& memory,
                       std::uint32_t first_symbol,
                       const std::vector<std::uint32_t>& chains) {
    const std::uint32_t count = static_cast<std::uint32_t>(chains.size());
    if (count == 0 ||
        !write_u32(memory, kSysvHash, 1) ||
        !write_u32(memory, kSysvHash + 4U, count) ||
        !write_u32(memory, kSysvHash + 8U, first_symbol)) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!write_u32(memory, kSysvHash + 12U + i * 4U, chains[i])) {
            return false;
        }
    }
    return true;
}

std::uint32_t test_gnu_hash(std::string_view name) {
    std::uint32_t hash = 5381U;
    for (const unsigned char byte : name) {
        hash = hash * 33U + byte;
    }
    return hash;
}

bool stage_gnu_lookup(LinearGuestMemory& memory,
                      const std::vector<std::uint32_t>& chains) {
    if (chains.empty() ||
        !write_u32(memory, kGnuHash, 1) ||
        !write_u32(memory, kGnuHash + 4U, 1) ||
        !write_u32(memory, kGnuHash + 8U, 1) ||
        !write_u32(memory, kGnuHash + 12U, 5) ||
        !write_u32(memory, kGnuHash + 16U, 0xffffffffU) ||
        !write_u32(memory, kGnuHash + 20U, 1)) {
        return false;
    }
    for (std::uint32_t i = 0; i < chains.size(); ++i) {
        if (!write_u32(memory, kGnuHash + 24U + i * 4U, chains[i])) {
            return false;
        }
    }
    return true;
}

int test_hash_headers_and_symbol_candidates_avoid_tiny_duplicate_reads() {
    RecordingGuestMemory sysv_memory{0x6000, kMemoryBase};
    if (!write_string(sysv_memory.backing(), 1, "target") ||
        !write_symbol(
            sysv_memory.backing(), 1, 1, 0x100, 4, 1, 2, 0, 1) ||
        !stage_sysv_lookup(sysv_memory.backing(), 1, {0, 0})) {
        return fail("could not stage SysV read-shape fixture");
    }

    sysv_memory.reset_reads();
    const auto sysv_index = build_elf32_symbol_index(
        sysv_memory, metadata(true, false), options());
    if (!sysv_index ||
        sysv_memory.count_reads(kSysvHash, 8U) != 1U ||
        sysv_memory.count_reads(kSysvHash, 4U) != 0U ||
        sysv_memory.count_reads(kSysvHash + 4U, 4U) != 0U) {
        return fail("SysV hash header was not read as one bounded block");
    }

    sysv_memory.reset_reads();
    const auto found = lookup_elf32_symbol(
        sysv_memory,
        0x4000,
        metadata(true, false),
        sysv_index.index,
        "target",
        options());
    if (!found ||
        sysv_memory.count_reads(kSymbolTable + 16U, 16U) != 1U) {
        return fail("symbol candidate entry was read more than once");
    }

    RecordingGuestMemory gnu_memory{0x6000, kMemoryBase};
    const std::uint32_t hash = test_gnu_hash("target");
    if (!write_string(gnu_memory.backing(), 1, "target") ||
        !write_symbol(
            gnu_memory.backing(), 1, 1, 0x100, 4, 1, 2, 0, 1) ||
        !stage_gnu_lookup(gnu_memory.backing(), {hash | 1U})) {
        return fail("could not stage GNU read-shape fixture");
    }

    gnu_memory.reset_reads();
    const auto gnu_index = build_elf32_symbol_index(
        gnu_memory, metadata(false, true), options());
    if (!gnu_index ||
        gnu_memory.count_reads(kGnuHash, 16U) != 1U ||
        gnu_memory.count_reads(kGnuHash, 4U) != 0U ||
        gnu_memory.count_reads(kGnuHash + 4U, 4U) != 0U ||
        gnu_memory.count_reads(kGnuHash + 8U, 4U) != 0U ||
        gnu_memory.count_reads(kGnuHash + 12U, 4U) != 0U) {
        return fail("GNU hash header was not read as one bounded block");
    }
    return 0;
}

int test_indexing_is_read_only() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    if (!stage_sysv(memory, 2, 4, {1, 0}, {0, 2, 3, 0})) {
        return fail("could not stage read-only SysV hash");
    }
    const std::array<std::uint8_t, 4> sentinel{0xde, 0xad, 0xbe, 0xef};
    if (!memory.write(kStringTable, sentinel)) {
        return fail("could not stage read-only sentinel");
    }

    const auto result =
        build_elf32_symbol_index(memory, metadata(true, false), options());
    if (!result) return fail("read-only indexing setup failed");

    std::array<std::uint8_t, 4> after{};
    if (!memory.read(kStringTable, after) || after != sentinel) {
        return fail("symbol indexing mutated guest memory");
    }
    return 0;
}

int test_sysv_exact_lookup_and_collision() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    if (!write_string(memory, 1, "other") ||
        !write_string(memory, 16, "target") ||
        !write_symbol(memory, 1, 1, 0x100, 4, 1, 2, 0, 1) ||
        !write_symbol(memory, 2, 16, 0x200, 8, 1, 2, 0, 1) ||
        !stage_sysv_lookup(memory, 1, {0, 2, 0})) {
        return fail("could not stage SysV lookup fixture");
    }

    const auto index =
        build_elf32_symbol_index(memory, metadata(true, false), options());
    if (!index) return fail("SysV lookup index construction failed");

    const auto found = lookup_elf32_symbol(
        memory, 0x4000, metadata(true, false), index.index, "target", options());
    if (!found || found.symbol.symbol_index != 2 ||
        found.symbol.name != "target" ||
        found.symbol.symbol.binding != 1 ||
        found.symbol.symbol.type != 2 ||
        found.symbol.guest_value != 0x4200) {
        return fail("SysV exact-name lookup did not return the expected symbol");
    }

    const auto missing = lookup_elf32_symbol(
        memory, 0x4000, metadata(true, false), index.index, "missing", options());
    if (missing.error != Elf32SymbolLookupError::SymbolNotFound) {
        return fail("SysV lookup miss was not classified");
    }
    return 0;
}

int test_gnu_exact_lookup() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    constexpr std::string_view first_name = "alpha";
    constexpr std::string_view second_name = "target";
    const std::uint32_t first_hash = test_gnu_hash(first_name);
    const std::uint32_t second_hash = test_gnu_hash(second_name);

    if (!write_string(memory, 1, first_name) ||
        !write_string(memory, 16, second_name) ||
        !write_symbol(memory, 1, 1, 0x100, 4, 1, 1, 0, 1) ||
        !write_symbol(memory, 2, 16, 0x280, 4, 1, 2, 0, 1) ||
        !stage_gnu_lookup(memory, {first_hash & ~1U, second_hash | 1U})) {
        return fail("could not stage GNU lookup fixture");
    }

    const auto index =
        build_elf32_symbol_index(memory, metadata(false, true), options());
    if (!index || index.index.symbol_count != 3) {
        return fail("GNU lookup index construction failed");
    }

    const auto found = lookup_elf32_symbol(
        memory, 0x4000, metadata(false, true), index.index,
        second_name, options());
    if (!found || found.symbol.symbol_index != 2 ||
        found.symbol.guest_value != 0x4280) {
        return fail("GNU exact-name lookup did not return the expected symbol");
    }
    return 0;
}

int test_eligibility_and_weak_first() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "target") ||
            !write_symbol(memory, 1, 1, 0x100, 4, 0, 2, 0, 1) ||
            !write_symbol(memory, 2, 1, 0x200, 4, 1, 2, 0, 0) ||
            !write_symbol(memory, 3, 1, 0x300, 4, 1, 2, 2, 1) ||
            !write_symbol(memory, 4, 1, 0x400, 4, 1, 2, 1, 1) ||
            !write_symbol(memory, 5, 1, 0x555, 4, 2, 2, 3, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 2, 3, 4, 5, 0})) {
            return fail("could not stage eligibility lookup fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto found = lookup_elf32_symbol(
            memory, 0x4000, metadata(true, false), index.index,
            "target", options());
        if (!found || found.symbol.symbol_index != 5 ||
            found.symbol.symbol.binding != 2 ||
            found.symbol.symbol.visibility != 3 ||
            found.symbol.guest_value != 0x4555) {
            return fail("local/undefined/hidden filtering or weak/protected lookup failed");
        }
    }

    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "target") ||
            !write_symbol(memory, 1, 1, 0x100, 4, 2, 2, 0, 1) ||
            !write_symbol(memory, 2, 1, 0x200, 4, 1, 2, 0, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 2, 0})) {
            return fail("could not stage weak-first fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto found = lookup_elf32_symbol(
            memory, 0x4000, metadata(true, false), index.index,
            "target", options());
        if (!found || found.symbol.symbol_index != 1 ||
            found.symbol.symbol.binding != 2) {
            return fail("later strong symbol incorrectly replaced earlier weak definition");
        }
    }
    return 0;
}

int test_absolute_and_value_overflow() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "absolute") ||
            !write_symbol(memory, 1, 1, 0x12345678U, 4, 1, 1, 0, 0xfff1) ||
            !stage_sysv_lookup(memory, 1, {0, 0})) {
            return fail("could not stage absolute-symbol fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto found = lookup_elf32_symbol(
            memory, 0xf0000000U, metadata(true, false), index.index,
            "absolute", options());
        if (!found || found.symbol.guest_value != 0x12345678U) {
            return fail("SHN_ABS symbol was incorrectly rebased");
        }
    }

    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "target") ||
            !write_symbol(memory, 1, 1, 0x40, 4, 1, 1, 0, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 0})) {
            return fail("could not stage value-overflow fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto result = lookup_elf32_symbol(
            memory, 0xfffffff0U, metadata(true, false), index.index,
            "target", options());
        if (result.error != Elf32SymbolLookupError::ValueOverflow) {
            return fail("resolved guest-value overflow was not rejected");
        }
    }
    return 0;
}

int test_matching_unsupported_forms() {
    struct Case {
        std::uint8_t binding;
        std::uint8_t type;
        std::uint8_t other;
        std::uint16_t section_index;
        Elf32SymbolLookupError expected;
    };
    constexpr std::array cases{
        Case{3, 2, 0, 1, Elf32SymbolLookupError::UnsupportedBinding},
        Case{1, 6, 0, 1, Elf32SymbolLookupError::UnsupportedType},
        Case{1, 10, 0, 1, Elf32SymbolLookupError::UnsupportedType},
        Case{1, 1, 4, 1, Elf32SymbolLookupError::UnsupportedVisibility},
        Case{1, 1, 0, 0xfff2, Elf32SymbolLookupError::UnsupportedSectionIndex},
        Case{1, 1, 0, 0xffff, Elf32SymbolLookupError::UnsupportedSectionIndex},
    };

    for (const Case& item : cases) {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "target") ||
            !write_symbol(memory, 1, 1, 0x100, 4,
                          item.binding, item.type, item.other,
                          item.section_index) ||
            !stage_sysv_lookup(memory, 1, {0, 0})) {
            return fail("could not stage unsupported-symbol fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto result = lookup_elf32_symbol(
            memory, 0x4000, metadata(true, false), index.index,
            "target", options());
        if (result.error != item.expected) {
            return fail("matching unsupported symbol form was not rejected explicitly");
        }
    }
    return 0;
}

int test_version_string_and_chain_failures() {
    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "target") ||
            !write_symbol(memory, 1, 1, 0x100, 4, 1, 2, 0, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 0})) {
            return fail("could not stage versioning fixture");
        }
        auto md = metadata(true, false);
        const auto index = build_elf32_symbol_index(memory, md, options());
        md.has_symbol_versioning = true;
        if (lookup_elf32_symbol(memory, 0x4000, md, index.index,
                                "target", options()).error !=
            Elf32SymbolLookupError::InvalidVersionMetadata) {
            return fail("inconsistent version marker was not rejected");
        }
        if (lookup_elf32_symbol(memory, 0x4000, metadata(true, false),
                                index.index, "", options()).error !=
            Elf32SymbolLookupError::InvalidLookupName) {
            return fail("empty lookup name was not rejected");
        }
    }

    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_symbol(memory, 1, 0x100, 0x100, 4, 1, 2, 0, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 0})) {
            return fail("could not stage bad-name fixture");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto result = lookup_elf32_symbol(
            memory, 0x4000, metadata(true, false), index.index,
            "target", options());
        if (result.error != Elf32SymbolLookupError::StringReadFailed ||
            result.string_error !=
                liba32android::elf::Elf32LinkerStringError::StringOffsetOutOfRange) {
            return fail("candidate string-table failure was not preserved");
        }
    }

    {
        LinearGuestMemory memory(0x6000, kMemoryBase);
        if (!write_string(memory, 1, "other") ||
            !write_symbol(memory, 1, 1, 0x100, 4, 1, 2, 0, 1) ||
            !write_symbol(memory, 2, 1, 0x200, 4, 1, 2, 0, 1) ||
            !stage_sysv_lookup(memory, 1, {0, 2, 1})) {
            return fail("could not stage cyclic SysV chain");
        }
        const auto index =
            build_elf32_symbol_index(memory, metadata(true, false), options());
        const auto result = lookup_elf32_symbol(
            memory, 0x4000, metadata(true, false), index.index,
            "missing", options());
        if (result.error != Elf32SymbolLookupError::InvalidHashChain) {
            return fail("cyclic SysV hash chain was not bounded/rejected");
        }
    }
    return 0;
}

int test_lookup_is_read_only() {
    LinearGuestMemory memory(0x6000, kMemoryBase);
    if (!write_string(memory, 1, "target") ||
        !write_symbol(memory, 1, 1, 0x100, 4, 1, 2, 0, 1) ||
        !stage_sysv_lookup(memory, 1, {0, 0})) {
        return fail("could not stage read-only lookup fixture");
    }

    const std::array<std::uint8_t, 4> sentinel{0xde, 0xad, 0xbe, 0xef};
    constexpr std::uint32_t sentinel_address = 0x5000;
    if (!memory.write(sentinel_address, sentinel)) {
        return fail("could not stage lookup sentinel");
    }

    const auto index =
        build_elf32_symbol_index(memory, metadata(true, false), options());
    const auto result = lookup_elf32_symbol(
        memory, 0x4000, metadata(true, false), index.index,
        "target", options());
    if (!result) return fail("read-only lookup setup failed");

    std::array<std::uint8_t, 4> after{};
    if (!memory.read(sentinel_address, after) || after != sentinel) {
        return fail("symbol lookup mutated unrelated guest bytes");
    }
    return 0;
}

struct GraphLayout {
    std::uint32_t strings{};
    std::uint32_t symbols{};
    std::uint32_t hash{};
};

GraphLayout graph_layout(std::size_t slot) {
    const std::uint32_t base =
        0x8000U + static_cast<std::uint32_t>(slot) * 0x400U;
    return GraphLayout{
        .strings = base,
        .symbols = base + 0x100U,
        .hash = base + 0x200U,
    };
}

bool write_string_at(LinearGuestMemory& memory,
                     std::uint32_t string_table,
                     std::uint32_t offset,
                     std::string_view value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    return memory.write(string_table + offset, bytes);
}

bool write_symbol_at(LinearGuestMemory& memory,
                     std::uint32_t symbol_table,
                     std::uint32_t index,
                     std::uint32_t name_offset,
                     std::uint32_t value,
                     std::uint8_t binding,
                     std::uint8_t type,
                     std::uint8_t visibility,
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
    bytes[13] = visibility;
    bytes[14] = static_cast<std::uint8_t>(section_index);
    bytes[15] = static_cast<std::uint8_t>(section_index >> 8U);
    return memory.write(symbol_table + index * 16U, bytes);
}

bool stage_graph_symbol(LinearGuestMemory& memory,
                        Elf32LoadedDependencyObject& object,
                        std::size_t slot,
                        std::string_view name,
                        std::uint32_t load_bias,
                        std::uint32_t value,
                        std::uint8_t binding = 1) {
    const GraphLayout layout = graph_layout(slot);
    object.identity = "graph-object-" + std::to_string(slot);
    object.load.load_bias = load_bias;
    object.linker_metadata.string_table =
        Elf32StringTableMetadata{.guest_address = layout.strings, .size = 0x80};
    object.linker_metadata.symbol_table =
        Elf32SymbolTableMetadata{.guest_address = layout.symbols, .entry_size = 16};
    object.linker_metadata.sysv_hash_table =
        Elf32HashTableMetadata{.guest_address = layout.hash};

    if (!write_string_at(memory, layout.strings, 1, name) ||
        !write_symbol_at(memory, layout.symbols, 1, 1, value,
                         binding, 2, 0, 1) ||
        !write_u32(memory, layout.hash, 1) ||
        !write_u32(memory, layout.hash + 4U, 2) ||
        !write_u32(memory, layout.hash + 8U, 1) ||
        !write_u32(memory, layout.hash + 12U, 0) ||
        !write_u32(memory, layout.hash + 16U, 0)) {
        return false;
    }
    return true;
}

void set_identity(Elf32LoadedDependencyObject& object, std::string_view identity) {
    object.identity.assign(identity.begin(), identity.end());
}

int test_graph_bfs_ignores_object_vector_order() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    graph.objects.resize(5);

    set_identity(graph.objects[0], "A");
    set_identity(graph.objects[3], "B");
    set_identity(graph.objects[2], "C");
    set_identity(graph.objects[1], "D");
    set_identity(graph.objects[4], "E");

    // Object-vector order is A,D,C,B,E, but dependency BFS is A,B,C,D,E.
    if (!stage_graph_symbol(memory, graph.objects[1], 1, "target",
                            0x1000, 0x111) ||
        !stage_graph_symbol(memory, graph.objects[2], 2, "target",
                            0x2000, 0x222)) {
        return fail("could not stage graph BFS symbols");
    }
    graph.objects[1].identity = "D";
    graph.objects[2].identity = "C";

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "B", .target_object = 3},
        Elf32DependencyEdge{.requested_name = "C", .target_object = 2},
    };
    graph.objects[3].dependencies = {
        Elf32DependencyEdge{.requested_name = "D", .target_object = 1},
    };
    graph.objects[2].dependencies = {
        Elf32DependencyEdge{.requested_name = "E", .target_object = 4},
    };

    const auto result = lookup_elf32_graph_symbol(
        memory, graph, 0, "target", options());
    if (!result || result.symbol.object_index != 2 ||
        result.symbol.symbol.guest_value != 0x2222) {
        return fail("graph lookup used discovery/vector order instead of BFS edge order");
    }
    return 0;
}

int test_graph_cycles_shared_and_scope_limit() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    graph.objects.resize(4);
    set_identity(graph.objects[0], "A");
    set_identity(graph.objects[1], "D");
    set_identity(graph.objects[2], "C");
    set_identity(graph.objects[3], "B");

    if (!stage_graph_symbol(memory, graph.objects[1], 1, "target",
                            0x3000, 0x123)) {
        return fail("could not stage shared graph target");
    }
    graph.objects[1].identity = "D";

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "B", .target_object = 3},
        Elf32DependencyEdge{.requested_name = "B-again", .target_object = 3},
        Elf32DependencyEdge{.requested_name = "C", .target_object = 2},
    };
    graph.objects[3].dependencies = {
        Elf32DependencyEdge{.requested_name = "D", .target_object = 1},
    };
    graph.objects[2].dependencies = {
        Elf32DependencyEdge{.requested_name = "D-shared", .target_object = 1},
    };
    graph.objects[1].dependencies = {
        Elf32DependencyEdge{.requested_name = "A-cycle", .target_object = 0},
    };

    const auto found = lookup_elf32_graph_symbol(
        memory, graph, 0, "target", options());
    if (!found || found.symbol.object_index != 1 ||
        found.symbol.symbol.guest_value != 0x3123) {
        return fail("cycle/shared dependency BFS lookup did not terminate correctly");
    }

    auto limited = options();
    limited.max_scope_objects = 3;
    const auto blocked = lookup_elf32_graph_symbol(
        memory, graph, 0, "target", limited);
    if (blocked.error != Elf32GraphSymbolLookupError::ScopeLimitExceeded ||
        !blocked.failing_object.has_value() ||
        *blocked.failing_object != 1) {
        return fail("graph scope-object ceiling was not enforced deterministically");
    }
    return 0;
}

int test_graph_weak_first_and_malformed_earlier_object() {
    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        Elf32DependencyGraph graph;
        graph.objects.resize(3);
        set_identity(graph.objects[0], "A");
        if (!stage_graph_symbol(memory, graph.objects[1], 1, "target",
                                0x1000, 0x100, 2) ||
            !stage_graph_symbol(memory, graph.objects[2], 2, "target",
                                0x2000, 0x200, 1)) {
            return fail("could not stage weak/global graph symbols");
        }
        graph.objects[1].identity = "weak-B";
        graph.objects[2].identity = "global-C";
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "B", .target_object = 1},
            Elf32DependencyEdge{.requested_name = "C", .target_object = 2},
        };

        const auto result = lookup_elf32_graph_symbol(
            memory, graph, 0, "target", options());
        if (!result || result.symbol.object_index != 1 ||
            result.symbol.symbol.symbol.binding != 2) {
            return fail("later global incorrectly replaced earlier weak graph definition");
        }
    }

    {
        LinearGuestMemory memory(0x20000, kMemoryBase);
        Elf32DependencyGraph graph;
        graph.objects.resize(3);
        set_identity(graph.objects[0], "A");
        set_identity(graph.objects[1], "malformed-B");

        const GraphLayout bad = graph_layout(1);
        graph.objects[1].linker_metadata.string_table =
            Elf32StringTableMetadata{.guest_address = bad.strings, .size = 0x80};
        graph.objects[1].linker_metadata.symbol_table =
            Elf32SymbolTableMetadata{.guest_address = bad.symbols, .entry_size = 16};

        if (!stage_graph_symbol(memory, graph.objects[2], 2, "target",
                                0x2000, 0x200)) {
            return fail("could not stage later valid graph symbol");
        }
        graph.objects[2].identity = "valid-C";
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "B", .target_object = 1},
            Elf32DependencyEdge{.requested_name = "C", .target_object = 2},
        };

        const auto result = lookup_elf32_graph_symbol(
            memory, graph, 0, "target", options());
        if (result.error != Elf32GraphSymbolLookupError::IndexBuildFailed ||
            !result.failing_object.has_value() ||
            *result.failing_object != 1 ||
            result.index_error != Elf32SymbolIndexError::MissingHashTable) {
            return fail("malformed earlier searchable object was silently skipped");
        }
    }
    return 0;
}

int test_reference_global_scope_and_symbolic_ordering() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    graph.objects.resize(3);

    if (!stage_graph_symbol(memory, graph.objects[0], 0, "target",
                            0x1000, 0x100) ||
        !stage_graph_symbol(memory, graph.objects[1], 1, "target",
                            0x2000, 0x200) ||
        !stage_graph_symbol(memory, graph.objects[2], 2, "target",
                            0x3000, 0x300)) {
        return fail("could not stage requester/global/local scope symbols");
    }
    graph.objects[0].identity = "requester";
    graph.objects[1].identity = "global";
    graph.objects[2].identity = "local-dependency";
    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "local", .target_object = 2},
    };

    const std::array<std::size_t, 1> global_scope{1};
    auto scoped = options();
    scoped.global_scope_objects = global_scope;

    const auto ordinary = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", scoped);
    if (!ordinary || ordinary.symbol.object_index != 1 ||
        ordinary.symbol.symbol.guest_value != 0x2200) {
        return fail("ordinary reference did not prefer explicit global scope");
    }

    const auto plain_graph = lookup_elf32_graph_symbol(
        memory, graph, 0, "target", scoped);
    if (!plain_graph || plain_graph.symbol.object_index != 0 ||
        plain_graph.symbol.symbol.guest_value != 0x1100) {
        return fail("plain graph lookup was incorrectly widened by reference global scope");
    }

    graph.objects[0].linker_metadata.symbolic = true;
    const auto symbolic = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", scoped);
    if (!symbolic || symbolic.symbol.object_index != 0 ||
        symbolic.symbol.symbol.guest_value != 0x1100) {
        return fail("DT_SYMBOLIC requester did not bind itself before global scope");
    }

    graph.objects[0].linker_metadata.symbolic = false;
    const std::array<std::size_t, 1> invalid_global{9};
    auto invalid = options();
    invalid.global_scope_objects = invalid_global;
    const auto bad = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", invalid);
    if (bad.error != Elf32GraphSymbolLookupError::InvalidGlobalScopeObject ||
        !bad.failing_object.has_value() || *bad.failing_object != 9) {
        return fail("invalid explicit global-scope object was not rejected");
    }

    // Preflight the complete explicit scope: a valid earlier definition must
    // not hide a later malformed caller-provided object index.
    const std::array<std::size_t, 2> trailing_invalid_global{1, 9};
    auto trailing_invalid = options();
    trailing_invalid.global_scope_objects = trailing_invalid_global;
    const auto hidden_bad = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "target", trailing_invalid);
    if (hidden_bad.error !=
            Elf32GraphSymbolLookupError::InvalidGlobalScopeObject ||
        !hidden_bad.failing_object.has_value() ||
        *hidden_bad.failing_object != 9) {
        return fail("trailing invalid global-scope object was hidden by an earlier match");
    }

    auto limited = options();
    limited.max_scope_objects = 1;
    limited.global_scope_objects = global_scope;
    const auto capped = lookup_elf32_graph_symbol_for_reference(
        memory, graph, 0, 1, "missing", limited);
    if (capped.error != Elf32GraphSymbolLookupError::ScopeLimitExceeded ||
        !capped.failing_object.has_value() || *capped.failing_object != 0) {
        return fail("global plus local reference scope did not share one object ceiling");
    }
    return 0;
}

int test_link_map_global_scope_feeds_reference_lookup() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32LinkMap link_map;
    link_map.graph.objects.resize(2);

    if (!stage_graph_symbol(memory, link_map.graph.objects[0], 0, "target",
                            0x1000, 0x100) ||
        !stage_graph_symbol(memory, link_map.graph.objects[1], 1, "target",
                            0x2000, 0x200)) {
        return fail("could not stage link-map global-scope lookup symbols");
    }
    link_map.global_scope_objects = {1};

    auto scoped = options();
    scoped.global_scope_objects = link_map.global_scope();
    const auto result = lookup_elf32_graph_symbol_for_reference(
        memory, link_map.graph, 0, 1, "target", scoped);
    if (!result || result.symbol.object_index != 1 ||
        result.symbol.symbol.guest_value != 0x2200) {
        return fail("feature-015 lookup did not consume link-map global scope directly");
    }
    return 0;
}

int test_graph_invalid_inputs_and_not_found() {
    LinearGuestMemory memory(0x20000, kMemoryBase);
    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    set_identity(graph.objects[0], "A");

    if (lookup_elf32_graph_symbol(memory, graph, 1, "target", options()).error !=
        Elf32GraphSymbolLookupError::InvalidGraphStart) {
        return fail("invalid graph start index was not rejected");
    }

    auto zero_scope = options();
    zero_scope.max_scope_objects = 0;
    if (lookup_elf32_graph_symbol(memory, graph, 0, "target", zero_scope).error !=
        Elf32GraphSymbolLookupError::InvalidOptions) {
        return fail("zero graph scope limit was not rejected");
    }

    if (lookup_elf32_graph_symbol(memory, graph, 0, "target", options()).error !=
        Elf32GraphSymbolLookupError::SymbolNotFound) {
        return fail("graph with no searchable objects did not return not-found");
    }

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "bad", .target_object = 9},
    };
    if (lookup_elf32_graph_symbol(memory, graph, 0, "target", options()).error !=
        Elf32GraphSymbolLookupError::InvalidGraphEdge) {
        return fail("invalid dependency edge target was not rejected");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_required_inputs_and_options(); status != 0) return status;
    if (const int status = test_valid_sysv_index(); status != 0) return status;
    if (const int status = test_sysv_failures(); status != 0) return status;
    if (const int status = test_valid_gnu_index(); status != 0) return status;
    if (const int status = test_gnu_failures(); status != 0) return status;
    if (const int status = test_dual_hash_consistency(); status != 0) return status;
    if (const int status = test_read_failures_and_symbol_range(); status != 0) return status;
    if (const int status =
            test_hash_headers_and_symbol_candidates_avoid_tiny_duplicate_reads();
        status != 0) return status;
    if (const int status = test_indexing_is_read_only(); status != 0) return status;
    if (const int status = test_sysv_exact_lookup_and_collision(); status != 0) return status;
    if (const int status = test_gnu_exact_lookup(); status != 0) return status;
    if (const int status = test_eligibility_and_weak_first(); status != 0) return status;
    if (const int status = test_absolute_and_value_overflow(); status != 0) return status;
    if (const int status = test_matching_unsupported_forms(); status != 0) return status;
    if (const int status = test_version_string_and_chain_failures(); status != 0) return status;
    if (const int status = test_lookup_is_read_only(); status != 0) return status;
    if (const int status = test_graph_bfs_ignores_object_vector_order(); status != 0) return status;
    if (const int status = test_graph_cycles_shared_and_scope_limit(); status != 0) return status;
    if (const int status = test_graph_weak_first_and_malformed_earlier_object(); status != 0) return status;
    if (const int status = test_reference_global_scope_and_symbolic_ordering(); status != 0) return status;
    if (const int status = test_link_map_global_scope_feeds_reference_lookup(); status != 0) return status;
    if (const int status = test_graph_invalid_inputs_and_not_found(); status != 0) return status;
    return 0;
}
