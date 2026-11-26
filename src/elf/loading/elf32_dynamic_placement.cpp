#include "elf/elf32_dynamic_placement.h"

#include <algorithm>
#include <cstdint>

#include "elf/elf32_load_plan.h"
#include "memory/guest_va_allocator.h"

namespace liba32android::elf {
namespace {

[[nodiscard]] Elf32DynamicPlacementResult failure(
    Elf32DynamicPlacementError error,
    Elf32LoadError load_error = Elf32LoadError::None) noexcept {
    Elf32DynamicPlacementResult result;
    result.error = error;
    result.load_error = load_error;
    return result;
}

}  // namespace

Elf32DynamicPlacementResult place_elf32_dynamic(
    const memory::MappedGuestMemory& memory,
    std::span<const std::uint8_t> image,
    const Elf32DynamicPlacementOptions& options) {
    const std::uint64_t address_space_size = memory::MappedGuestMemory::kAddressSpaceSize;
    if (options.search_end_exclusive > address_space_size ||
        static_cast<std::uint64_t>(options.search_begin) >= options.search_end_exclusive) {
        return failure(Elf32DynamicPlacementError::InvalidSearchWindow);
    }

    const auto plan_result = plan_elf32_load(memory, image);
    if (!plan_result) {
        return failure(Elf32DynamicPlacementError::InvalidImage, plan_result.error);
    }

    const Elf32LoadPlan& plan = plan_result.plan;
    if (plan.type != Elf32ImageType::Dynamic) {
        return failure(Elf32DynamicPlacementError::NotDynamic);
    }
    if (plan.maximum_page_end <= plan.minimum_page) {
        return failure(Elf32DynamicPlacementError::AddressOverflow);
    }

    const std::uint64_t span = plan.maximum_page_end - plan.minimum_page;
    const std::uint64_t alignment =
        std::max<std::uint64_t>(memory.page_size(), plan.required_load_bias_alignment);
    const std::uint64_t effective_begin =
        std::max<std::uint64_t>(options.search_begin, plan.minimum_page);

    if (effective_begin >= options.search_end_exclusive ||
        effective_begin > UINT32_MAX) {
        return failure(Elf32DynamicPlacementError::NoSpace);
    }

    memory::GuestVaSearchOptions search_options{
        .begin = static_cast<std::uint32_t>(effective_begin),
        .end_exclusive = options.search_end_exclusive,
        .alignment = alignment,
        .alignment_offset = plan.minimum_page % alignment,
    };

    const auto search = memory::find_free_guest_range(memory, span, search_options);
    if (!search) {
        switch (search.error) {
        case memory::GuestVaSearchError::InvalidRange:
            return failure(Elf32DynamicPlacementError::NoSpace);
        case memory::GuestVaSearchError::InvalidAlignment:
        case memory::GuestVaSearchError::SizeOverflow:
            return failure(Elf32DynamicPlacementError::AddressOverflow);
        case memory::GuestVaSearchError::NoSpace:
            return failure(Elf32DynamicPlacementError::NoSpace);
        case memory::GuestVaSearchError::None:
            break;
        }
        return failure(Elf32DynamicPlacementError::AddressOverflow);
    }

    Elf32DynamicPlacementResult result;
    result.dynamic_base = search.address;
    return result;
}

const char* to_string(Elf32DynamicPlacementError error) noexcept {
    switch (error) {
    case Elf32DynamicPlacementError::None:
        return "none";
    case Elf32DynamicPlacementError::InvalidImage:
        return "invalid_image";
    case Elf32DynamicPlacementError::NotDynamic:
        return "not_dynamic";
    case Elf32DynamicPlacementError::InvalidSearchWindow:
        return "invalid_search_window";
    case Elf32DynamicPlacementError::AddressOverflow:
        return "address_overflow";
    case Elf32DynamicPlacementError::NoSpace:
        return "no_space";
    }
    return "unknown";
}

}  // namespace liba32android::elf
