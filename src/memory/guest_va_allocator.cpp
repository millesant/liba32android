#include "memory/guest_va_allocator.h"

#include <cstdint>
#include <limits>

namespace liba32android::memory {
namespace {

[[nodiscard]] GuestVaSearchResult failure(GuestVaSearchError error) noexcept {
    GuestVaSearchResult result;
    result.error = error;
    return result;
}

}  // namespace

GuestVaSearchResult find_free_guest_range(const MappedGuestMemory& memory,
                                          std::uint64_t length,
                                          const GuestVaSearchOptions& options) {
    const std::uint64_t page_size = memory.page_size();
    const std::uint64_t address_space_size = MappedGuestMemory::kAddressSpaceSize;

    if (length == 0 || (length % page_size) != 0 ||
        options.end_exclusive > address_space_size ||
        static_cast<std::uint64_t>(options.begin) >= options.end_exclusive) {
        return failure(GuestVaSearchError::InvalidRange);
    }
    if (length > address_space_size) {
        return failure(GuestVaSearchError::SizeOverflow);
    }
    if (options.alignment == 0 || options.alignment > address_space_size ||
        (options.alignment % page_size) != 0) {
        return failure(GuestVaSearchError::InvalidAlignment);
    }

    const std::uint64_t alignment_offset = options.alignment_offset % options.alignment;
    if ((alignment_offset % page_size) != 0) {
        return failure(GuestVaSearchError::InvalidAlignment);
    }

    const std::uint64_t begin = options.begin;
    const std::uint64_t begin_remainder = begin % options.alignment;
    const std::uint64_t delta =
        begin_remainder <= alignment_offset
            ? alignment_offset - begin_remainder
            : options.alignment - (begin_remainder - alignment_offset);

    if (delta > address_space_size - begin) {
        return failure(GuestVaSearchError::NoSpace);
    }

    std::uint64_t candidate = begin + delta;
    while (candidate < options.end_exclusive) {
        if (candidate > address_space_size - length ||
            length > options.end_exclusive - candidate) {
            return failure(GuestVaSearchError::NoSpace);
        }

        bool free = true;
        const std::uint64_t candidate_end = candidate + length;
        for (std::uint64_t page = candidate; page < candidate_end; page += page_size) {
            if (memory.is_mapped(static_cast<std::uint32_t>(page))) {
                free = false;
                break;
            }
        }

        if (free) {
            GuestVaSearchResult result;
            result.address = static_cast<std::uint32_t>(candidate);
            return result;
        }

        if (options.alignment > address_space_size - candidate) {
            break;
        }
        candidate += options.alignment;
    }

    return failure(GuestVaSearchError::NoSpace);
}

const char* to_string(GuestVaSearchError error) noexcept {
    switch (error) {
    case GuestVaSearchError::None:
        return "none";
    case GuestVaSearchError::InvalidRange:
        return "invalid_range";
    case GuestVaSearchError::InvalidAlignment:
        return "invalid_alignment";
    case GuestVaSearchError::SizeOverflow:
        return "size_overflow";
    case GuestVaSearchError::NoSpace:
        return "no_space";
    }
    return "unknown";
}

}  // namespace liba32android::memory
