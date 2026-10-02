#include "elf/elf32_dynamic.h"

#include "elf/internal/elf32_bytes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace liba32android::elf {
namespace {

constexpr std::size_t kElf32DynamicEntrySize = 8;
constexpr std::size_t kDynamicReadChunkBytes = 256;
constexpr std::int32_t kDynamicTagNull = 0;

static_assert((kDynamicReadChunkBytes % kElf32DynamicEntrySize) == 0);
constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;

[[nodiscard]] Elf32DynamicResult failure(Elf32DynamicError error) {
    Elf32DynamicResult result;
    result.error = error;
    return result;
}

}  // namespace

Elf32DynamicResult parse_elf32_dynamic(const memory::GuestMemory& memory,
                                       const Elf32DynamicSegment& segment) {
    if (segment.file_size > segment.memory_size ||
        static_cast<std::uint64_t>(segment.guest_address) + segment.memory_size >
            kGuestAddressSpaceSize) {
        return failure(Elf32DynamicError::InvalidRange);
    }
    if ((segment.file_size % kElf32DynamicEntrySize) != 0) {
        return failure(Elf32DynamicError::TruncatedEntry);
    }

    Elf32DynamicResult result;
    std::array<std::uint8_t, kDynamicReadChunkBytes> chunk{};
    for (std::uint64_t chunk_offset = 0; chunk_offset < segment.file_size;) {
        const std::size_t chunk_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                kDynamicReadChunkBytes,
                static_cast<std::uint64_t>(segment.file_size) - chunk_offset));
        const auto chunk_address = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(segment.guest_address) + chunk_offset);
        const bool batch_read = memory.read(
            chunk_address,
            std::span<std::uint8_t>{chunk}.first(chunk_size));

        for (std::size_t local_offset = 0;
             local_offset < chunk_size;
             local_offset += kElf32DynamicEntrySize) {
            std::array<std::uint8_t, kElf32DynamicEntrySize> single{};
            const std::uint8_t* entry_bytes = nullptr;
            if (batch_read) {
                entry_bytes = chunk.data() + local_offset;
            } else {
                const auto entry_address = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(chunk_address) + local_offset);
                if (!memory.read(entry_address, single)) {
                    return failure(Elf32DynamicError::ReadFailed);
                }
                entry_bytes = single.data();
            }

            const std::uint32_t raw_tag =
                detail::decode_u32_le(entry_bytes);
            const Elf32DynamicEntry entry{
                .tag = std::bit_cast<std::int32_t>(raw_tag),
                .value = detail::decode_u32_le(entry_bytes + 4),
            };
            result.entries.push_back(entry);
            if (entry.tag == kDynamicTagNull) {
                return result;
            }
        }

        chunk_offset += chunk_size;
    }

    return failure(Elf32DynamicError::Unterminated);
}

const char* to_string(Elf32DynamicError error) noexcept {
    switch (error) {
    case Elf32DynamicError::None: return "none";
    case Elf32DynamicError::InvalidRange: return "invalid_range";
    case Elf32DynamicError::TruncatedEntry: return "truncated_entry";
    case Elf32DynamicError::ReadFailed: return "read_failed";
    case Elf32DynamicError::Unterminated: return "unterminated";
    }
    return "unknown";
}

}  // namespace liba32android::elf
