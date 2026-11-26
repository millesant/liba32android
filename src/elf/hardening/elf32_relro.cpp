#include "elf/elf32_relro.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace liba32android::elf {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;

struct PlannedRelroPage {
    std::uint32_t address{};
    memory::MemoryPermission original_permissions{memory::MemoryPermission::None};
};

[[nodiscard]] Elf32RelroSealResult failure(
    Elf32RelroError error,
    std::optional<std::uint32_t> failing_page = std::nullopt) {
    Elf32RelroSealResult result;
    result.error = error;
    result.primary_error = error;
    result.failing_page = failing_page;
    return result;
}

}  // namespace

Elf32RelroSealResult seal_elf32_gnu_relro(
    memory::MappedGuestMemory& memory,
    const Elf32LoadResult& load,
    const Elf32RelroOptions& options) {
    if (load.relro_segments.empty()) {
        return {};
    }
    if (options.max_pages == 0) {
        return failure(Elf32RelroError::InvalidOptions);
    }

    const std::uint64_t page_size = memory.page_size();
    if (page_size == 0) {
        return failure(Elf32RelroError::InvalidMetadata);
    }

    std::uint64_t declared_pages = 0;
    std::vector<std::uint32_t> page_addresses;

    for (const Elf32RelroSegment& relro : load.relro_segments) {
        if (relro.memory_size == 0 ||
            relro.mapping_size == 0 ||
            (static_cast<std::uint64_t>(relro.mapping_start) % page_size) != 0 ||
            (relro.mapping_size % page_size) != 0) {
            return failure(Elf32RelroError::InvalidMetadata);
        }

        const std::uint64_t exact_start = relro.guest_address;
        const std::uint64_t exact_end = exact_start + relro.memory_size;
        const std::uint64_t mapping_start = relro.mapping_start;
        const std::uint64_t mapping_end = mapping_start + relro.mapping_size;
        if (exact_end > kGuestAddressSpaceSize ||
            mapping_end > kGuestAddressSpaceSize ||
            exact_start < mapping_start ||
            exact_end > mapping_end) {
            return failure(Elf32RelroError::InvalidMetadata);
        }

        const std::uint64_t page_count = relro.mapping_size / page_size;
        if (page_count == 0 ||
            declared_pages > options.max_pages ||
            page_count > static_cast<std::uint64_t>(options.max_pages) - declared_pages) {
            return failure(Elf32RelroError::PageLimitExceeded);
        }
        declared_pages += page_count;

        for (std::uint64_t page = mapping_start;
             page < mapping_end;
             page += page_size) {
            if (page > std::numeric_limits<std::uint32_t>::max()) {
                return failure(Elf32RelroError::InvalidMetadata);
            }
            page_addresses.push_back(static_cast<std::uint32_t>(page));
        }
    }

    std::sort(page_addresses.begin(), page_addresses.end());
    page_addresses.erase(
        std::unique(page_addresses.begin(), page_addresses.end()),
        page_addresses.end());

    std::vector<PlannedRelroPage> pages;
    pages.reserve(page_addresses.size());
    for (const std::uint32_t page : page_addresses) {
        if (!memory.is_mapped(page)) {
            return failure(Elf32RelroError::UnmappedPage, page);
        }

        const memory::MemoryPermission permissions = memory.permissions(page);
        if (!memory::has_permission(permissions, memory::MemoryPermission::Read)) {
            return failure(Elf32RelroError::UnreadablePage, page);
        }
        if (memory::has_permission(permissions, memory::MemoryPermission::Execute)) {
            return failure(Elf32RelroError::ExecutablePage, page);
        }

        pages.push_back({
            .address = page,
            .original_permissions = permissions,
        });
    }

    std::vector<std::size_t> changed;
    changed.reserve(pages.size());

    for (std::size_t index = 0; index < pages.size(); ++index) {
        const PlannedRelroPage& page = pages[index];
        if (!memory::has_permission(
                page.original_permissions, memory::MemoryPermission::Write)) {
            continue;
        }

        if (!memory.protect(
                page.address,
                static_cast<std::size_t>(page_size),
                memory::MemoryPermission::Read)) {
            for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
                const PlannedRelroPage& prior = pages[*it];
                if (!memory.protect(
                        prior.address,
                        static_cast<std::size_t>(page_size),
                        prior.original_permissions) ||
                    memory.permissions(prior.address) != prior.original_permissions) {
                    Elf32RelroSealResult result;
                    result.error = Elf32RelroError::RollbackFailed;
                    result.primary_error = Elf32RelroError::ProtectFailed;
                    result.failing_page = page.address;
                    result.rollback_failing_page = prior.address;
                    return result;
                }
            }
            return failure(Elf32RelroError::ProtectFailed, page.address);
        }
        changed.push_back(index);
    }

    Elf32RelroSealResult result;
    result.sealed_pages = static_cast<std::uint32_t>(pages.size());
    return result;
}

const char* to_string(Elf32RelroError error) noexcept {
    switch (error) {
    case Elf32RelroError::None: return "none";
    case Elf32RelroError::InvalidOptions: return "invalid_options";
    case Elf32RelroError::InvalidMetadata: return "invalid_metadata";
    case Elf32RelroError::PageLimitExceeded: return "page_limit_exceeded";
    case Elf32RelroError::UnmappedPage: return "unmapped_page";
    case Elf32RelroError::UnreadablePage: return "unreadable_page";
    case Elf32RelroError::ExecutablePage: return "executable_page";
    case Elf32RelroError::ProtectFailed: return "protect_failed";
    case Elf32RelroError::RollbackFailed: return "rollback_failed";
    }
    return "unknown";
}

}  // namespace liba32android::elf
