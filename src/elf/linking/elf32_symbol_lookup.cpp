#include "elf/elf32_symbol_lookup.h"

#include "elf/elf32_symbol_versioning.h"
#include "elf/internal/elf32_bytes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <span>
#include <vector>

namespace liba32android::elf {
namespace {

constexpr std::uint32_t kElf32SymbolEntrySize = 16;
constexpr std::uint32_t kElf32WordBits = 32;
constexpr std::uint8_t kStbLocal = 0;
constexpr std::uint8_t kStbGlobal = 1;
constexpr std::uint8_t kStbWeak = 2;
constexpr std::uint8_t kSttNotype = 0;
constexpr std::uint8_t kSttObject = 1;
constexpr std::uint8_t kSttFunc = 2;
constexpr std::uint8_t kSttGnuIfunc = 10;
constexpr std::uint8_t kStvDefault = 0;
constexpr std::uint8_t kStvInternal = 1;
constexpr std::uint8_t kStvHidden = 2;
constexpr std::uint8_t kStvProtected = 3;
constexpr std::uint16_t kShnUndef = 0;
constexpr std::uint16_t kShnLoReserve = 0xff00;
constexpr std::uint16_t kShnAbs = 0xfff1;
constexpr std::uint16_t kShnCommon = 0xfff2;
constexpr std::uint16_t kShnXindex = 0xffff;
constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;
constexpr std::size_t kReadValidationChunkSize = 256;

[[nodiscard]] Elf32SymbolIndexResult failure(Elf32SymbolIndexError error) {
    Elf32SymbolIndexResult result;
    result.error = error;
    return result;
}

[[nodiscard]] Elf32ObjectSymbolLookupResult lookup_failure(
    Elf32SymbolLookupError error,
    Elf32LinkerStringError string_error = Elf32LinkerStringError::None) {
    Elf32ObjectSymbolLookupResult result;
    result.error = error;
    result.string_error = string_error;
    return result;
}

[[nodiscard]] Elf32GraphSymbolLookupResult graph_failure(
    Elf32GraphSymbolLookupError error,
    std::optional<std::size_t> failing_object = std::nullopt) {
    Elf32GraphSymbolLookupResult result;
    result.error = error;
    result.failing_object = failing_object;
    return result;
}

[[nodiscard]] Elf32SymbolVersionOptions version_options(
    const Elf32SymbolLookupOptions& options) noexcept {
    return Elf32SymbolVersionOptions{
        .max_records = options.max_version_records,
        .max_name_bytes = options.max_name_bytes,
    };
}

[[nodiscard]] Elf32SymbolLookupError version_lookup_error(
    Elf32SymbolVersionError error) noexcept {
    switch (error) {
    case Elf32SymbolVersionError::None:
        return Elf32SymbolLookupError::None;
    case Elf32SymbolVersionError::InvalidOptions:
        return Elf32SymbolLookupError::InvalidOptions;
    case Elf32SymbolVersionError::ReadFailed:
        return Elf32SymbolLookupError::VersionReadFailed;
    case Elf32SymbolVersionError::StringReadFailed:
        return Elf32SymbolLookupError::StringReadFailed;
    case Elf32SymbolVersionError::RecordLimitExceeded:
        return Elf32SymbolLookupError::VersionRecordLimitExceeded;
    case Elf32SymbolVersionError::VersionIndexNotFound:
        return Elf32SymbolLookupError::VersionIndexNotFound;
    case Elf32SymbolVersionError::DependencyNotFound:
        return Elf32SymbolLookupError::VersionDependencyNotFound;
    case Elf32SymbolVersionError::InvalidMetadata:
    case Elf32SymbolVersionError::InvalidRecord:
        return Elf32SymbolLookupError::InvalidVersionMetadata;
    }
    return Elf32SymbolLookupError::InvalidVersionMetadata;
}

[[nodiscard]] bool read_u32(const memory::GuestMemory& memory,
                            std::uint32_t address,
                            std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = detail::decode_u32_le(bytes.data());
    return true;
}

[[nodiscard]] bool range_fits(std::uint32_t address, std::uint64_t size) {
    return static_cast<std::uint64_t>(address) + size <=
           kGuestAddressSpaceSize;
}

[[nodiscard]] bool readable_range(const memory::GuestMemory& memory,
                                  std::uint32_t address,
                                  std::uint64_t size) {
    if (size == 0) return true;
    if (!range_fits(address, size)) return false;

    std::array<std::uint8_t, kReadValidationChunkSize> buffer{};
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), size - offset));
        const std::uint64_t current =
            static_cast<std::uint64_t>(address) + offset;
        if (current > std::numeric_limits<std::uint32_t>::max() ||
            !memory.read(static_cast<std::uint32_t>(current),
                         std::span<std::uint8_t>(buffer.data(), chunk))) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool read_indexed_word(const memory::GuestMemory& memory,
                                     std::uint32_t base,
                                     std::uint32_t index,
                                     std::uint32_t& value) {
    std::uint32_t address = 0;
    if (!detail::checked_add_guest_address(base, static_cast<std::uint64_t>(index) * 4U, address)) {
        return false;
    }
    return read_u32(memory, address, value);
}

[[nodiscard]] std::uint32_t sysv_hash(std::string_view name) noexcept {
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

[[nodiscard]] std::uint32_t gnu_hash(std::string_view name) noexcept {
    std::uint32_t hash = 5381U;
    for (const unsigned char byte : name) {
        hash = hash * 33U + byte;
    }
    return hash;
}

[[nodiscard]] bool read_symbol(const memory::GuestMemory& memory,
                               const Elf32SymbolTableMetadata& table,
                               std::uint32_t symbol_index,
                               Elf32Symbol& symbol) {
    std::uint32_t address = 0;
    if (!detail::checked_add_guest_address(table.guest_address,
                     static_cast<std::uint64_t>(symbol_index) *
                         kElf32SymbolEntrySize,
                     address)) {
        return false;
    }

    std::array<std::uint8_t, kElf32SymbolEntrySize> bytes{};
    if (!memory.read(address, bytes)) return false;

    symbol.name_offset = detail::decode_u32_le(bytes.data());
    symbol.value = detail::decode_u32_le(bytes.data() + 4);
    symbol.size = detail::decode_u32_le(bytes.data() + 8);
    symbol.binding = static_cast<std::uint8_t>(bytes[12] >> 4U);
    symbol.type = static_cast<std::uint8_t>(bytes[12] & 0x0fU);
    symbol.raw_other = bytes[13];
    symbol.visibility = static_cast<std::uint8_t>(symbol.raw_other & 0x03U);
    symbol.section_index = detail::decode_u16_le(bytes.data() + 14);
    return true;
}

}  // namespace

Elf32SymbolIndexResult build_elf32_symbol_index(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolLookupOptions& options) {
    if (options.max_symbols == 0 ||
        options.max_hash_buckets == 0 ||
        options.max_gnu_bloom_words == 0) {
        return failure(Elf32SymbolIndexError::InvalidOptions);
    }
    if (!metadata.string_table.has_value()) {
        return failure(Elf32SymbolIndexError::MissingStringTable);
    }
    if (!metadata.symbol_table.has_value()) {
        return failure(Elf32SymbolIndexError::MissingSymbolTable);
    }
    if (!metadata.sysv_hash_table.has_value() &&
        !metadata.gnu_hash_table.has_value()) {
        return failure(Elf32SymbolIndexError::MissingHashTable);
    }

    Elf32SymbolIndexResult result;
    std::optional<std::uint32_t> sysv_symbol_count;

    if (metadata.sysv_hash_table.has_value()) {
        const std::uint32_t hash_address =
            metadata.sysv_hash_table->guest_address;

        std::array<std::uint8_t, 8> header_bytes{};
        if (!range_fits(hash_address, header_bytes.size()) ||
            !memory.read(hash_address, header_bytes)) {
            return failure(Elf32SymbolIndexError::HashHeaderReadFailed);
        }
        const std::uint32_t bucket_count =
            detail::decode_u32_le(header_bytes.data());
        const std::uint32_t chain_count =
            detail::decode_u32_le(header_bytes.data() + 4);

        if (bucket_count == 0 || chain_count == 0) {
            return failure(Elf32SymbolIndexError::InvalidSysvHash);
        }
        if (bucket_count > options.max_hash_buckets) {
            return failure(Elf32SymbolIndexError::HashLimitExceeded);
        }
        if (chain_count > options.max_symbols) {
            return failure(Elf32SymbolIndexError::SymbolCountExceeded);
        }

        const std::uint64_t bucket_bytes =
            static_cast<std::uint64_t>(bucket_count) * 4U;
        const std::uint64_t chain_bytes =
            static_cast<std::uint64_t>(chain_count) * 4U;
        const std::uint64_t total_bytes = 8U + bucket_bytes + chain_bytes;
        if (!range_fits(hash_address, total_bytes)) {
            return failure(Elf32SymbolIndexError::HashRangeOverflow);
        }

        std::uint32_t buckets_address = 0;
        std::uint32_t chains_address = 0;
        if (!detail::checked_add_guest_address(hash_address, 8U, buckets_address) ||
            !detail::checked_add_guest_address(buckets_address, bucket_bytes, chains_address)) {
            return failure(Elf32SymbolIndexError::HashRangeOverflow);
        }
        if (!readable_range(memory, hash_address, total_bytes)) {
            return failure(Elf32SymbolIndexError::HashReadFailed);
        }

        for (std::uint32_t i = 0; i < bucket_count; ++i) {
            std::uint32_t symbol_index = 0;
            if (!read_indexed_word(memory, buckets_address, i, symbol_index)) {
                return failure(Elf32SymbolIndexError::HashReadFailed);
            }
            if (symbol_index >= chain_count && symbol_index != 0) {
                return failure(Elf32SymbolIndexError::HashIndexOutOfRange);
            }
        }
        for (std::uint32_t i = 0; i < chain_count; ++i) {
            std::uint32_t next_index = 0;
            if (!read_indexed_word(memory, chains_address, i, next_index)) {
                return failure(Elf32SymbolIndexError::HashReadFailed);
            }
            if (next_index >= chain_count && next_index != 0) {
                return failure(Elf32SymbolIndexError::HashIndexOutOfRange);
            }
        }

        result.index.sysv_hash = Elf32SysvHashIndex{
            .bucket_count = bucket_count,
            .chain_count = chain_count,
            .buckets_guest_address = buckets_address,
            .chains_guest_address = chains_address,
        };
        sysv_symbol_count = chain_count;
        result.index.symbol_count = chain_count;
    }

    if (metadata.gnu_hash_table.has_value()) {
        const std::uint32_t hash_address =
            metadata.gnu_hash_table->guest_address;

        std::array<std::uint8_t, 16> header_bytes{};
        if (!range_fits(hash_address, header_bytes.size()) ||
            !memory.read(hash_address, header_bytes)) {
            return failure(Elf32SymbolIndexError::HashHeaderReadFailed);
        }

        const std::uint32_t bucket_count =
            detail::decode_u32_le(header_bytes.data());
        const std::uint32_t symbol_offset =
            detail::decode_u32_le(header_bytes.data() + 4);
        const std::uint32_t bloom_word_count =
            detail::decode_u32_le(header_bytes.data() + 8);
        const std::uint32_t bloom_shift =
            detail::decode_u32_le(header_bytes.data() + 12);

        if (bucket_count == 0 || symbol_offset == 0 ||
            !is_power_of_two(bloom_word_count) ||
            bloom_shift >= kElf32WordBits) {
            return failure(Elf32SymbolIndexError::InvalidGnuHash);
        }
        if (bucket_count > options.max_hash_buckets ||
            bloom_word_count > options.max_gnu_bloom_words) {
            return failure(Elf32SymbolIndexError::HashLimitExceeded);
        }
        if (symbol_offset > options.max_symbols) {
            return failure(Elf32SymbolIndexError::SymbolCountExceeded);
        }

        const std::uint64_t bloom_bytes =
            static_cast<std::uint64_t>(bloom_word_count) * 4U;
        const std::uint64_t bucket_bytes =
            static_cast<std::uint64_t>(bucket_count) * 4U;
        const std::uint64_t prefix_bytes = 16U + bloom_bytes + bucket_bytes;
        if (!range_fits(hash_address, prefix_bytes)) {
            return failure(Elf32SymbolIndexError::HashRangeOverflow);
        }

        std::uint32_t bloom_address = 0;
        std::uint32_t buckets_address = 0;
        std::uint32_t chains_address = 0;
        if (!detail::checked_add_guest_address(hash_address, 16U, bloom_address) ||
            !detail::checked_add_guest_address(bloom_address, bloom_bytes, buckets_address) ||
            !detail::checked_add_guest_address(buckets_address, bucket_bytes, chains_address)) {
            return failure(Elf32SymbolIndexError::HashRangeOverflow);
        }
        if (!readable_range(memory, hash_address, prefix_bytes)) {
            return failure(Elf32SymbolIndexError::HashReadFailed);
        }

        std::uint32_t maximum_bucket = 0;
        for (std::uint32_t i = 0; i < bucket_count; ++i) {
            std::uint32_t symbol_index = 0;
            if (!read_indexed_word(memory, buckets_address, i, symbol_index)) {
                return failure(Elf32SymbolIndexError::HashReadFailed);
            }
            if (symbol_index == 0) continue;
            if (symbol_index < symbol_offset) {
                return failure(Elf32SymbolIndexError::InvalidGnuHash);
            }
            if (symbol_index >= options.max_symbols) {
                return failure(Elf32SymbolIndexError::SymbolCountExceeded);
            }
            if (sysv_symbol_count.has_value() &&
                symbol_index >= *sysv_symbol_count) {
                return failure(Elf32SymbolIndexError::HashIndexOutOfRange);
            }
            maximum_bucket = std::max(maximum_bucket, symbol_index);
        }

        if (sysv_symbol_count.has_value()) {
            if (symbol_offset > *sysv_symbol_count) {
                return failure(Elf32SymbolIndexError::InvalidGnuHash);
            }
            const std::uint64_t chain_count =
                static_cast<std::uint64_t>(*sysv_symbol_count) -
                symbol_offset;
            const std::uint64_t chain_bytes = chain_count * 4U;
            if (!range_fits(chains_address, chain_bytes)) {
                return failure(Elf32SymbolIndexError::HashRangeOverflow);
            }
            if (!readable_range(memory, chains_address, chain_bytes)) {
                return failure(Elf32SymbolIndexError::HashReadFailed);
            }

            // SysV nchain supplies the authoritative dynsym extent, but every
            // nonempty GNU bucket must still reach a low-bit chain terminator
            // inside that same finite extent. Validate this now so a later GNU
            // lookup cannot silently accept inconsistent dual-hash metadata.
            for (std::uint32_t bucket_index = 0;
                 bucket_index < bucket_count; ++bucket_index) {
                std::uint32_t bucket_symbol = 0;
                if (!read_indexed_word(memory, buckets_address, bucket_index,
                                       bucket_symbol)) {
                    return failure(Elf32SymbolIndexError::HashReadFailed);
                }
                if (bucket_symbol == 0) continue;

                bool terminated = false;
                for (std::uint32_t symbol_index = bucket_symbol;
                     symbol_index < *sysv_symbol_count; ++symbol_index) {
                    const std::uint64_t chain_index =
                        static_cast<std::uint64_t>(symbol_index) -
                        symbol_offset;
                    std::uint32_t chain_hash = 0;
                    if (!read_indexed_word(
                            memory, chains_address,
                            static_cast<std::uint32_t>(chain_index),
                            chain_hash)) {
                        return failure(Elf32SymbolIndexError::HashReadFailed);
                    }
                    if ((chain_hash & 1U) != 0) {
                        terminated = true;
                        break;
                    }
                }
                if (!terminated) {
                    return failure(
                        Elf32SymbolIndexError::UnterminatedGnuChain);
                }
            }
        } else if (maximum_bucket == 0) {
            result.index.symbol_count = symbol_offset;
        } else {
            std::uint32_t symbol_index = maximum_bucket;
            bool terminated = false;
            while (symbol_index < options.max_symbols) {
                const std::uint64_t chain_index =
                    static_cast<std::uint64_t>(symbol_index) - symbol_offset;
                std::uint32_t chain_address = 0;
                if (!detail::checked_add_guest_address(chains_address, chain_index * 4U,
                                 chain_address)) {
                    return failure(Elf32SymbolIndexError::HashRangeOverflow);
                }

                std::uint32_t chain_hash = 0;
                if (!read_u32(memory, chain_address, chain_hash)) {
                    return failure(Elf32SymbolIndexError::HashReadFailed);
                }
                if ((chain_hash & 1U) != 0) {
                    if (symbol_index ==
                        std::numeric_limits<std::uint32_t>::max()) {
                        return failure(
                            Elf32SymbolIndexError::SymbolCountExceeded);
                    }
                    result.index.symbol_count = symbol_index + 1U;
                    terminated = true;
                    break;
                }
                ++symbol_index;
            }
            if (!terminated) {
                return failure(
                    Elf32SymbolIndexError::UnterminatedGnuChain);
            }
        }

        result.index.gnu_hash = Elf32GnuHashIndex{
            .bucket_count = bucket_count,
            .symbol_offset = symbol_offset,
            .bloom_word_count = bloom_word_count,
            .bloom_shift = bloom_shift,
            .bloom_guest_address = bloom_address,
            .buckets_guest_address = buckets_address,
            .chains_guest_address = chains_address,
        };
    }

    if (result.index.symbol_count == 0) {
        return failure(Elf32SymbolIndexError::InvalidGnuHash);
    }
    if (result.index.symbol_count > options.max_symbols) {
        return failure(Elf32SymbolIndexError::SymbolCountExceeded);
    }

    const std::uint64_t symbol_bytes =
        static_cast<std::uint64_t>(result.index.symbol_count) *
        kElf32SymbolEntrySize;
    if (!range_fits(metadata.symbol_table->guest_address, symbol_bytes)) {
        return failure(Elf32SymbolIndexError::SymbolRangeOverflow);
    }
    if (!readable_range(memory, metadata.symbol_table->guest_address,
                        symbol_bytes)) {
        return failure(Elf32SymbolIndexError::SymbolReadFailed);
    }

    return result;
}

Elf32SymbolReadResult read_elf32_symbol_entry(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolIndex& index,
    std::uint32_t symbol_index) {
    Elf32SymbolReadResult result;
    if (!metadata.symbol_table.has_value() ||
        metadata.symbol_table->entry_size != kElf32SymbolEntrySize ||
        index.symbol_count == 0) {
        result.error = Elf32SymbolReadError::InvalidMetadata;
        return result;
    }
    if (symbol_index >= index.symbol_count) {
        result.error = Elf32SymbolReadError::SymbolIndexOutOfRange;
        return result;
    }
    if (!read_symbol(memory, *metadata.symbol_table, symbol_index,
                     result.symbol)) {
        result.error = Elf32SymbolReadError::SymbolReadFailed;
        return result;
    }
    return result;
}

static Elf32ObjectSymbolLookupResult lookup_elf32_symbol_impl(
    const memory::GuestMemory& memory,
    std::uint32_t load_bias,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolIndex& index,
    std::string_view name,
    const Elf32SymbolLookupOptions& options,
    const std::optional<Elf32SymbolVersionRequirement>& requirement) {
    if (options.max_name_bytes == 0) {
        return lookup_failure(Elf32SymbolLookupError::InvalidOptions);
    }
    if (name.empty()) {
        return lookup_failure(Elf32SymbolLookupError::InvalidLookupName);
    }
    if (!metadata.string_table.has_value() ||
        !metadata.symbol_table.has_value() ||
        index.symbol_count == 0 ||
        (!index.gnu_hash.has_value() && !index.sysv_hash.has_value())) {
        return lookup_failure(Elf32SymbolLookupError::InvalidMetadata);
    }
    Elf32LoadedDependencyObject version_object;
    version_object.load.load_bias = load_bias;
    version_object.linker_metadata = metadata;

    const auto evaluate_candidate =
        [&](std::uint32_t symbol_index)
            -> std::optional<Elf32ObjectSymbolLookupResult> {
        if (symbol_index == 0 || symbol_index >= index.symbol_count) {
            return lookup_failure(
                Elf32SymbolLookupError::HashIndexOutOfRange);
        }

        Elf32Symbol symbol;
        if (!read_symbol(memory, *metadata.symbol_table, symbol_index,
                         symbol)) {
            return lookup_failure(Elf32SymbolLookupError::SymbolReadFailed);
        }

        const auto symbol_name = read_elf32_string_table_entry(
            memory, *metadata.string_table, symbol.name_offset,
            Elf32LinkerStringOptions{
                .max_string_bytes = options.max_name_bytes,
            });
        if (!symbol_name) {
            return lookup_failure(Elf32SymbolLookupError::StringReadFailed,
                                  symbol_name.error);
        }
        if (symbol_name.value != name) {
            return std::nullopt;
        }

        if (symbol.binding == kStbLocal ||
            symbol.section_index == kShnUndef ||
            symbol.visibility == kStvInternal ||
            symbol.visibility == kStvHidden) {
            return std::nullopt;
        }
        if (symbol.binding != kStbGlobal && symbol.binding != kStbWeak) {
            return lookup_failure(Elf32SymbolLookupError::UnsupportedBinding);
        }

        // read_symbol already preserved the complete st_other byte, so do not
        // re-read the same 16-byte symbol entry just to validate its upper
        // visibility bits.
        if ((symbol.raw_other & 0xfcU) != 0 ||
            (symbol.visibility != kStvDefault &&
             symbol.visibility != kStvProtected)) {
            return lookup_failure(
                Elf32SymbolLookupError::UnsupportedVisibility);
        }

        if (symbol.section_index == kShnCommon ||
            symbol.section_index == kShnXindex ||
            (symbol.section_index >= kShnLoReserve &&
             symbol.section_index != kShnAbs)) {
            return lookup_failure(
                Elf32SymbolLookupError::UnsupportedSectionIndex);
        }

        if (symbol.type != kSttNotype &&
            symbol.type != kSttObject &&
            symbol.type != kSttFunc) {
            return lookup_failure(Elf32SymbolLookupError::UnsupportedType);
        }
        if (symbol.type == kSttGnuIfunc) {
            return lookup_failure(Elf32SymbolLookupError::UnsupportedType);
        }

        const auto version_match = match_elf32_symbol_version(
            memory,
            version_object,
            symbol_index,
            requirement,
            version_options(options));
        if (!version_match) {
            return lookup_failure(
                version_lookup_error(version_match.error),
                version_match.string_error);
        }
        if (!version_match.matches) {
            return std::nullopt;
        }

        std::uint32_t guest_value = symbol.value;
        if (symbol.section_index != kShnAbs) {
            if (!detail::checked_add_guest_address(load_bias, symbol.value, guest_value)) {
                return lookup_failure(Elf32SymbolLookupError::ValueOverflow);
            }
        }

        Elf32ObjectSymbolLookupResult result;
        result.symbol.symbol_index = symbol_index;
        result.symbol.name = symbol_name.value;
        result.symbol.symbol = symbol;
        result.symbol.guest_value = guest_value;
        return result;
    };

    if (index.gnu_hash.has_value()) {
        const Elf32GnuHashIndex& hash_index = *index.gnu_hash;
        const std::uint32_t hash = gnu_hash(name);

        std::uint32_t bloom_word = 0;
        const std::uint32_t bloom_index =
            (hash / kElf32WordBits) &
            (hash_index.bloom_word_count - 1U);
        if (!read_indexed_word(memory, hash_index.bloom_guest_address,
                               bloom_index, bloom_word)) {
            return lookup_failure(Elf32SymbolLookupError::HashReadFailed);
        }
        const std::uint32_t first_bit =
            1U << (hash % kElf32WordBits);
        const std::uint32_t second_bit =
            1U << ((hash >> hash_index.bloom_shift) % kElf32WordBits);
        if ((bloom_word & first_bit) == 0 ||
            (bloom_word & second_bit) == 0) {
            return lookup_failure(Elf32SymbolLookupError::SymbolNotFound);
        }

        std::uint32_t symbol_index = 0;
        if (!read_indexed_word(
                memory, hash_index.buckets_guest_address,
                hash % hash_index.bucket_count, symbol_index)) {
            return lookup_failure(Elf32SymbolLookupError::HashReadFailed);
        }
        if (symbol_index == 0) {
            return lookup_failure(Elf32SymbolLookupError::SymbolNotFound);
        }
        if (symbol_index < hash_index.symbol_offset ||
            symbol_index >= index.symbol_count) {
            return lookup_failure(
                Elf32SymbolLookupError::HashIndexOutOfRange);
        }

        while (symbol_index < index.symbol_count) {
            const std::uint32_t chain_index =
                symbol_index - hash_index.symbol_offset;
            std::uint32_t chain_hash = 0;
            if (!read_indexed_word(memory, hash_index.chains_guest_address,
                                   chain_index, chain_hash)) {
                return lookup_failure(Elf32SymbolLookupError::HashReadFailed);
            }

            if ((chain_hash | 1U) == (hash | 1U)) {
                if (auto candidate = evaluate_candidate(symbol_index)) {
                    return *candidate;
                }
            }

            if ((chain_hash & 1U) != 0) {
                return lookup_failure(Elf32SymbolLookupError::SymbolNotFound);
            }
            ++symbol_index;
        }

        return lookup_failure(Elf32SymbolLookupError::InvalidHashChain);
    }

    const Elf32SysvHashIndex& hash_index = *index.sysv_hash;
    const std::uint32_t hash = sysv_hash(name);
    std::uint32_t symbol_index = 0;
    if (!read_indexed_word(memory, hash_index.buckets_guest_address,
                           hash % hash_index.bucket_count, symbol_index)) {
        return lookup_failure(Elf32SymbolLookupError::HashReadFailed);
    }
    if (symbol_index >= index.symbol_count && symbol_index != 0) {
        return lookup_failure(Elf32SymbolLookupError::HashIndexOutOfRange);
    }

    std::uint32_t hops = 0;
    while (symbol_index != 0 && hops < index.symbol_count) {
        if (auto candidate = evaluate_candidate(symbol_index)) {
            return *candidate;
        }

        std::uint32_t next = 0;
        if (!read_indexed_word(memory, hash_index.chains_guest_address,
                               symbol_index, next)) {
            return lookup_failure(Elf32SymbolLookupError::HashReadFailed);
        }
        if (next >= index.symbol_count && next != 0) {
            return lookup_failure(
                Elf32SymbolLookupError::HashIndexOutOfRange);
        }
        symbol_index = next;
        ++hops;
    }
    if (symbol_index != 0) {
        return lookup_failure(Elf32SymbolLookupError::InvalidHashChain);
    }
    return lookup_failure(Elf32SymbolLookupError::SymbolNotFound);
}

Elf32ObjectSymbolLookupResult lookup_elf32_symbol(
    const memory::GuestMemory& memory,
    std::uint32_t load_bias,
    const Elf32LinkerMetadata& metadata,
    const Elf32SymbolIndex& index,
    std::string_view name,
    const Elf32SymbolLookupOptions& options) {
    return lookup_elf32_symbol_impl(
        memory, load_bias, metadata, index, name, options, std::nullopt);
}

static Elf32GraphSymbolLookupResult lookup_elf32_graph_symbol_impl(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t start_object,
    std::string_view name,
    const Elf32SymbolLookupOptions& options,
    const std::optional<Elf32SymbolVersionRequirement>& requirement,
    std::span<const std::size_t> global_scope_objects,
    bool requester_symbolic) {
    if (options.max_scope_objects == 0) {
        return graph_failure(Elf32GraphSymbolLookupError::InvalidOptions);
    }
    if (start_object >= graph.objects.size()) {
        return graph_failure(
            Elf32GraphSymbolLookupError::InvalidGraphStart, start_object);
    }

    // The caller-provided scope is structural input. Validate the complete
    // ordered list before any candidate can short-circuit lookup so a trailing
    // invalid object index can never be hidden by an earlier definition.
    for (const std::size_t object_index : global_scope_objects) {
        if (object_index >= graph.objects.size()) {
            return graph_failure(
                Elf32GraphSymbolLookupError::InvalidGlobalScopeObject,
                object_index);
        }
    }

    std::vector<std::uint8_t> searched(graph.objects.size(), 0);
    std::vector<std::uint8_t> expanded(graph.objects.size(), 0);
    std::uint32_t scope_objects = 0;

    const auto search_object =
        [&](std::size_t object_index,
            Elf32GraphSymbolLookupError invalid_error)
            -> std::optional<Elf32GraphSymbolLookupResult> {
        if (object_index >= graph.objects.size()) {
            return graph_failure(invalid_error, object_index);
        }
        if (searched[object_index] != 0) return std::nullopt;
        if (scope_objects >= options.max_scope_objects) {
            return graph_failure(
                Elf32GraphSymbolLookupError::ScopeLimitExceeded,
                object_index);
        }

        searched[object_index] = 1;
        ++scope_objects;
        const Elf32LoadedDependencyObject& object =
            graph.objects[object_index];

        if (!object.linker_metadata.symbol_table.has_value()) {
            return std::nullopt;
        }

        const Elf32SymbolIndexResult index = build_elf32_symbol_index(
            memory, object.linker_metadata, options);
        if (!index) {
            auto result = graph_failure(
                Elf32GraphSymbolLookupError::IndexBuildFailed,
                object_index);
            result.index_error = index.error;
            return result;
        }

        const Elf32ObjectSymbolLookupResult lookup =
            lookup_elf32_symbol_impl(
                memory, object.load.load_bias,
                object.linker_metadata, index.index, name, options,
                requirement);
        if (lookup) {
            Elf32GraphSymbolLookupResult result;
            result.symbol.object_index = object_index;
            result.symbol.symbol = lookup.symbol;
            return result;
        }
        if (lookup.error != Elf32SymbolLookupError::SymbolNotFound) {
            auto result = graph_failure(
                Elf32GraphSymbolLookupError::ObjectLookupFailed,
                object_index);
            result.lookup_error = lookup.error;
            result.string_error = lookup.string_error;
            return result;
        }
        return std::nullopt;
    };

    if (requester_symbolic) {
        if (auto result = search_object(
                start_object, Elf32GraphSymbolLookupError::InvalidGraphStart)) {
            return *result;
        }
    }

    for (const std::size_t object_index : global_scope_objects) {
        if (auto result = search_object(
                object_index,
                Elf32GraphSymbolLookupError::InvalidGlobalScopeObject)) {
            return *result;
        }
    }

    std::deque<std::size_t> pending;
    pending.push_back(start_object);
    while (!pending.empty()) {
        const std::size_t object_index = pending.front();
        pending.pop_front();

        if (object_index >= graph.objects.size()) {
            return graph_failure(
                Elf32GraphSymbolLookupError::InvalidGraphEdge,
                object_index);
        }

        if (auto result = search_object(
                object_index, Elf32GraphSymbolLookupError::InvalidGraphEdge)) {
            return *result;
        }

        if (expanded[object_index] != 0) continue;
        expanded[object_index] = 1;
        const Elf32LoadedDependencyObject& object =
            graph.objects[object_index];
        for (const Elf32DependencyEdge& edge : object.dependencies) {
            if (edge.target_object >= graph.objects.size()) {
                return graph_failure(
                    Elf32GraphSymbolLookupError::InvalidGraphEdge,
                    object_index);
            }
            if (expanded[edge.target_object] == 0) {
                pending.push_back(edge.target_object);
            }
        }
    }

    return graph_failure(Elf32GraphSymbolLookupError::SymbolNotFound);
}

Elf32GraphSymbolLookupResult lookup_elf32_graph_symbol(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t start_object,
    std::string_view name,
    const Elf32SymbolLookupOptions& options) {
    return lookup_elf32_graph_symbol_impl(
        memory, graph, start_object, name, options, std::nullopt,
        std::span<const std::size_t>{}, false);
}

Elf32GraphSymbolLookupResult lookup_elf32_graph_symbol_for_reference(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t start_object,
    std::uint32_t reference_symbol_index,
    std::string_view name,
    const Elf32SymbolLookupOptions& options) {
    if (options.max_scope_objects == 0) {
        return graph_failure(Elf32GraphSymbolLookupError::InvalidOptions);
    }
    if (start_object >= graph.objects.size()) {
        return graph_failure(
            Elf32GraphSymbolLookupError::InvalidGraphStart, start_object);
    }

    const auto version = resolve_elf32_symbol_version_requirement(
        memory, graph, start_object, reference_symbol_index,
        version_options(options));
    if (!version) {
        auto result = graph_failure(
            Elf32GraphSymbolLookupError::ObjectLookupFailed, start_object);
        result.lookup_error = version_lookup_error(version.error);
        result.string_error = version.string_error;
        return result;
    }

    return lookup_elf32_graph_symbol_impl(
        memory, graph, start_object, name, options, version.requirement,
        options.global_scope_objects,
        graph.objects[start_object].linker_metadata.symbolic);
}

const char* to_string(Elf32SymbolIndexError error) noexcept {
    switch (error) {
    case Elf32SymbolIndexError::None: return "none";
    case Elf32SymbolIndexError::InvalidOptions: return "invalid_options";
    case Elf32SymbolIndexError::MissingStringTable: return "missing_string_table";
    case Elf32SymbolIndexError::MissingSymbolTable: return "missing_symbol_table";
    case Elf32SymbolIndexError::MissingHashTable: return "missing_hash_table";
    case Elf32SymbolIndexError::HashHeaderReadFailed: return "hash_header_read_failed";
    case Elf32SymbolIndexError::HashRangeOverflow: return "hash_range_overflow";
    case Elf32SymbolIndexError::HashReadFailed: return "hash_read_failed";
    case Elf32SymbolIndexError::InvalidSysvHash: return "invalid_sysv_hash";
    case Elf32SymbolIndexError::InvalidGnuHash: return "invalid_gnu_hash";
    case Elf32SymbolIndexError::HashLimitExceeded: return "hash_limit_exceeded";
    case Elf32SymbolIndexError::HashIndexOutOfRange: return "hash_index_out_of_range";
    case Elf32SymbolIndexError::UnterminatedGnuChain: return "unterminated_gnu_chain";
    case Elf32SymbolIndexError::SymbolCountExceeded: return "symbol_count_exceeded";
    case Elf32SymbolIndexError::SymbolRangeOverflow: return "symbol_range_overflow";
    case Elf32SymbolIndexError::SymbolReadFailed: return "symbol_read_failed";
    }
    return "unknown";
}

const char* to_string(Elf32SymbolReadError error) noexcept {
    switch (error) {
    case Elf32SymbolReadError::None: return "none";
    case Elf32SymbolReadError::InvalidMetadata: return "invalid_metadata";
    case Elf32SymbolReadError::SymbolIndexOutOfRange: return "symbol_index_out_of_range";
    case Elf32SymbolReadError::SymbolReadFailed: return "symbol_read_failed";
    }
    return "unknown";
}

const char* to_string(Elf32SymbolLookupError error) noexcept {
    switch (error) {
    case Elf32SymbolLookupError::None: return "none";
    case Elf32SymbolLookupError::InvalidOptions: return "invalid_options";
    case Elf32SymbolLookupError::InvalidMetadata: return "invalid_metadata";
    case Elf32SymbolLookupError::InvalidLookupName: return "invalid_lookup_name";
    case Elf32SymbolLookupError::UnsupportedVersioning: return "unsupported_versioning";
    case Elf32SymbolLookupError::VersionReadFailed: return "version_read_failed";
    case Elf32SymbolLookupError::InvalidVersionMetadata: return "invalid_version_metadata";
    case Elf32SymbolLookupError::VersionRecordLimitExceeded: return "version_record_limit_exceeded";
    case Elf32SymbolLookupError::VersionIndexNotFound: return "version_index_not_found";
    case Elf32SymbolLookupError::VersionDependencyNotFound: return "version_dependency_not_found";
    case Elf32SymbolLookupError::HashReadFailed: return "hash_read_failed";
    case Elf32SymbolLookupError::HashIndexOutOfRange: return "hash_index_out_of_range";
    case Elf32SymbolLookupError::InvalidHashChain: return "invalid_hash_chain";
    case Elf32SymbolLookupError::SymbolReadFailed: return "symbol_read_failed";
    case Elf32SymbolLookupError::StringReadFailed: return "string_read_failed";
    case Elf32SymbolLookupError::SymbolNotFound: return "symbol_not_found";
    case Elf32SymbolLookupError::UnsupportedBinding: return "unsupported_binding";
    case Elf32SymbolLookupError::UnsupportedType: return "unsupported_type";
    case Elf32SymbolLookupError::UnsupportedVisibility: return "unsupported_visibility";
    case Elf32SymbolLookupError::UnsupportedSectionIndex: return "unsupported_section_index";
    case Elf32SymbolLookupError::ValueOverflow: return "value_overflow";
    }
    return "unknown";
}

const char* to_string(Elf32GraphSymbolLookupError error) noexcept {
    switch (error) {
    case Elf32GraphSymbolLookupError::None: return "none";
    case Elf32GraphSymbolLookupError::InvalidOptions: return "invalid_options";
    case Elf32GraphSymbolLookupError::InvalidGraphStart: return "invalid_graph_start";
    case Elf32GraphSymbolLookupError::InvalidGraphEdge: return "invalid_graph_edge";
    case Elf32GraphSymbolLookupError::InvalidGlobalScopeObject: return "invalid_global_scope_object";
    case Elf32GraphSymbolLookupError::ScopeLimitExceeded: return "scope_limit_exceeded";
    case Elf32GraphSymbolLookupError::IndexBuildFailed: return "index_build_failed";
    case Elf32GraphSymbolLookupError::ObjectLookupFailed: return "object_lookup_failed";
    case Elf32GraphSymbolLookupError::SymbolNotFound: return "symbol_not_found";
    }
    return "unknown";
}

}  // namespace liba32android::elf
