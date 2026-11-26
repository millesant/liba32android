#include "elf/elf32_linker_strings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace liba32android::elf {
namespace {

constexpr std::size_t kReadChunkSize = 256;

[[nodiscard]] Elf32SingleStringResult single_failure(Elf32LinkerStringError error) {
    Elf32SingleStringResult result;
    result.error = error;
    return result;
}

[[nodiscard]] Elf32LinkerStringResult aggregate_failure(Elf32LinkerStringError error) {
    Elf32LinkerStringResult result;
    result.error = error;
    return result;
}

}  // namespace

Elf32SingleStringResult read_elf32_string_table_entry(
    const memory::GuestMemory& memory,
    const Elf32StringTableMetadata& string_table,
    std::uint32_t offset,
    const Elf32LinkerStringOptions& options) {
    if (offset >= string_table.size) {
        return single_failure(Elf32LinkerStringError::StringOffsetOutOfRange);
    }

    const std::uint64_t start =
        static_cast<std::uint64_t>(string_table.guest_address) + offset;
    if (start > std::numeric_limits<std::uint32_t>::max()) {
        return single_failure(Elf32LinkerStringError::AddressOverflow);
    }

    const std::uint64_t remaining =
        static_cast<std::uint64_t>(string_table.size) - offset;
    const std::uint64_t max_probe =
        static_cast<std::uint64_t>(options.max_string_bytes) + 1U;

    Elf32SingleStringResult result;
    result.value.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(options.max_string_bytes, kReadChunkSize)));

    std::array<std::uint8_t, kReadChunkSize> buffer{};
    std::uint64_t inspected = 0;

    while (inspected < remaining && inspected < max_probe) {
        const std::uint64_t chunk64 = std::min({
            static_cast<std::uint64_t>(buffer.size()),
            remaining - inspected,
            max_probe - inspected,
        });
        const std::size_t chunk = static_cast<std::size_t>(chunk64);

        const std::uint64_t current = start + inspected;
        const std::uint64_t last = current + chunk64 - 1U;
        if (current > std::numeric_limits<std::uint32_t>::max() ||
            last > std::numeric_limits<std::uint32_t>::max()) {
            return single_failure(Elf32LinkerStringError::AddressOverflow);
        }

        if (!memory.read(static_cast<std::uint32_t>(current),
                         std::span<std::uint8_t>(buffer.data(), chunk))) {
            return single_failure(Elf32LinkerStringError::ReadFailed);
        }

        for (std::size_t index = 0; index < chunk; ++index) {
            const std::uint64_t payload_position = inspected + index;
            const std::uint8_t byte = buffer[index];

            if (byte == 0) {
                return result;
            }

            if (payload_position >= options.max_string_bytes) {
                return single_failure(Elf32LinkerStringError::StringTooLong);
            }

            result.value.push_back(static_cast<char>(byte));
        }

        inspected += chunk64;
    }

    if (inspected >= remaining) {
        return single_failure(Elf32LinkerStringError::UnterminatedString);
    }
    return single_failure(Elf32LinkerStringError::StringTooLong);
}

Elf32LinkerStringResult build_elf32_linker_strings(
    const memory::GuestMemory& memory,
    const Elf32LinkerMetadata& metadata,
    const Elf32LinkerStringOptions& options) {
    const bool has_requested_strings =
        metadata.soname_offset.has_value() || !metadata.needed_offsets.empty();
    if (!has_requested_strings) {
        return {};
    }
    if (!metadata.string_table.has_value()) {
        return aggregate_failure(Elf32LinkerStringError::MissingStringTable);
    }

    Elf32LinkerStringResult result;
    const Elf32StringTableMetadata& table = *metadata.string_table;

    if (metadata.soname_offset.has_value()) {
        Elf32SingleStringResult soname =
            read_elf32_string_table_entry(memory, table, *metadata.soname_offset, options);
        if (!soname) {
            return aggregate_failure(soname.error);
        }
        result.strings.soname = std::move(soname.value);
    }

    result.strings.needed.reserve(metadata.needed_offsets.size());
    for (const std::uint32_t offset : metadata.needed_offsets) {
        Elf32SingleStringResult needed =
            read_elf32_string_table_entry(memory, table, offset, options);
        if (!needed) {
            return aggregate_failure(needed.error);
        }
        result.strings.needed.push_back(std::move(needed.value));
    }

    return result;
}

const char* to_string(Elf32LinkerStringError error) noexcept {
    switch (error) {
    case Elf32LinkerStringError::None:
        return "none";
    case Elf32LinkerStringError::MissingStringTable:
        return "missing_string_table";
    case Elf32LinkerStringError::StringOffsetOutOfRange:
        return "string_offset_out_of_range";
    case Elf32LinkerStringError::AddressOverflow:
        return "address_overflow";
    case Elf32LinkerStringError::ReadFailed:
        return "read_failed";
    case Elf32LinkerStringError::UnterminatedString:
        return "unterminated_string";
    case Elf32LinkerStringError::StringTooLong:
        return "string_too_long";
    }
    return "unknown";
}

}  // namespace liba32android::elf
