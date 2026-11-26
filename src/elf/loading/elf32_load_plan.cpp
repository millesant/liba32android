#include "elf/elf32_load_plan.h"

#include "elf/internal/elf32_bytes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace liba32android::elf {
namespace {

constexpr std::size_t kElf32HeaderSize = 52;
constexpr std::size_t kElf32ProgramHeaderSize = 32;
constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;

constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfDataLittleEndian = 1;
constexpr std::uint8_t kElfVersionCurrent = 1;
constexpr std::uint16_t kElfTypeExec = 2;
constexpr std::uint16_t kElfTypeDyn = 3;
constexpr std::uint16_t kElfMachineArm = 40;
constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint32_t kProgramTypeDynamic = 2;
constexpr std::uint32_t kProgramTypeGnuRelro = 0x6474e552U;
constexpr std::uint32_t kFlagExecute = 1U << 0;
constexpr std::uint32_t kFlagWrite = 1U << 1;
constexpr std::uint32_t kFlagRead = 1U << 2;

[[nodiscard]] std::uint64_t align_down(std::uint64_t value,
                                       std::uint64_t alignment) noexcept {
    return value - (value % alignment);
}

[[nodiscard]] bool align_up(std::uint64_t value,
                            std::uint64_t alignment,
                            std::uint64_t& output) noexcept {
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        output = value;
        return value <= kGuestAddressSpaceSize;
    }

    const std::uint64_t increment = alignment - remainder;
    if (value > kGuestAddressSpaceSize - increment) {
        return false;
    }

    output = value + increment;
    return true;
}

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool decode_permissions(
    std::uint32_t flags,
    memory::MemoryPermission& permissions) noexcept {
    const bool read = (flags & kFlagRead) != 0;
    const bool write = (flags & kFlagWrite) != 0;
    const bool execute = (flags & kFlagExecute) != 0;

    if ((!read && (write || execute)) || (write && execute)) {
        return false;
    }

    permissions = memory::MemoryPermission::None;
    if (read) {
        permissions = permissions | memory::MemoryPermission::Read;
    }
    if (write) {
        permissions = permissions | memory::MemoryPermission::Write;
    }
    if (execute) {
        permissions = permissions | memory::MemoryPermission::Execute;
    }
    return true;
}

[[nodiscard]] Elf32LoadPlanResult failure(Elf32LoadError error) {
    Elf32LoadPlanResult result;
    result.error = error;
    return result;
}

}  // namespace

Elf32LoadPlanResult plan_elf32_load(
    const memory::MappedGuestMemory& memory,
    std::span<const std::uint8_t> image) {
    if (image.size() < kElf32HeaderSize) {
        return failure(Elf32LoadError::TruncatedHeader);
    }

    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        return failure(Elf32LoadError::BadMagic);
    }
    if (image[4] != kElfClass32) {
        return failure(Elf32LoadError::UnsupportedClass);
    }
    if (image[5] != kElfDataLittleEndian) {
        return failure(Elf32LoadError::UnsupportedEndian);
    }
    if (image[6] != kElfVersionCurrent) {
        return failure(Elf32LoadError::UnsupportedIdentVersion);
    }

    const std::uint16_t type = detail::decode_u16_le(image, 16);
    const std::uint16_t machine = detail::decode_u16_le(image, 18);
    const std::uint32_t version = detail::decode_u32_le(image, 20);
    const std::uint32_t entry = detail::decode_u32_le(image, 24);
    const std::uint32_t program_header_offset = detail::decode_u32_le(image, 28);
    const std::uint16_t header_size = detail::decode_u16_le(image, 40);
    const std::uint16_t program_header_entry_size = detail::decode_u16_le(image, 42);
    const std::uint16_t program_header_count = detail::decode_u16_le(image, 44);

    if (version != kElfVersionCurrent) {
        return failure(Elf32LoadError::UnsupportedElfVersion);
    }
    if (type != kElfTypeDyn && type != kElfTypeExec) {
        return failure(Elf32LoadError::UnsupportedType);
    }
    if (machine != kElfMachineArm) {
        return failure(Elf32LoadError::UnsupportedMachine);
    }
    if (header_size != kElf32HeaderSize) {
        return failure(Elf32LoadError::InvalidHeaderSize);
    }
    if (program_header_entry_size != kElf32ProgramHeaderSize) {
        return failure(Elf32LoadError::InvalidProgramHeaderSize);
    }

    const std::uint64_t program_header_bytes =
        static_cast<std::uint64_t>(program_header_entry_size) * program_header_count;
    const std::uint64_t program_header_end =
        static_cast<std::uint64_t>(program_header_offset) + program_header_bytes;
    if (program_header_end > image.size()) {
        return failure(Elf32LoadError::ProgramHeaderTableOutOfBounds);
    }

    Elf32LoadPlan plan;
    plan.type = type == kElfTypeDyn ? Elf32ImageType::Dynamic : Elf32ImageType::Executable;
    plan.entry = entry;
    plan.minimum_page = kGuestAddressSpaceSize;

    const std::uint64_t page_size = memory.page_size();

    for (std::uint16_t index = 0; index < program_header_count; ++index) {
        const std::size_t offset = static_cast<std::size_t>(program_header_offset) +
                                   static_cast<std::size_t>(index) * kElf32ProgramHeaderSize;
        const std::uint32_t program_type = detail::decode_u32_le(image, offset);

        if (program_type == kProgramTypeDynamic) {
            if (plan.dynamic_segment.has_value()) {
                return failure(Elf32LoadError::MultipleDynamicSegments);
            }

            Elf32LoadPlanDynamicSegment dynamic;
            dynamic.offset = detail::decode_u32_le(image, offset + 4);
            dynamic.virtual_address = detail::decode_u32_le(image, offset + 8);
            dynamic.file_size = detail::decode_u32_le(image, offset + 16);
            dynamic.memory_size = detail::decode_u32_le(image, offset + 20);

            if (dynamic.memory_size == 0) {
                return failure(Elf32LoadError::DynamicSegmentEmpty);
            }
            if (dynamic.file_size > dynamic.memory_size) {
                return failure(Elf32LoadError::DynamicSegmentFileszExceedsMemsz);
            }
            if (static_cast<std::uint64_t>(dynamic.offset) + dynamic.file_size > image.size()) {
                return failure(Elf32LoadError::DynamicSegmentFileOutOfBounds);
            }
            if (static_cast<std::uint64_t>(dynamic.virtual_address) + dynamic.memory_size >
                kGuestAddressSpaceSize) {
                return failure(Elf32LoadError::DynamicSegmentAddressOverflow);
            }

            plan.dynamic_segment = dynamic;
            continue;
        }

        if (program_type == kProgramTypeGnuRelro) {
            Elf32LoadPlanRelroSegment relro;
            relro.virtual_address = detail::decode_u32_le(image, offset + 8);
            relro.memory_size = detail::decode_u32_le(image, offset + 20);

            if (relro.memory_size == 0) {
                return failure(Elf32LoadError::RelroSegmentEmpty);
            }

            const std::uint64_t relro_start = relro.virtual_address;
            const std::uint64_t relro_end = relro_start + relro.memory_size;
            if (relro_end > kGuestAddressSpaceSize) {
                return failure(Elf32LoadError::RelroSegmentAddressOverflow);
            }

            relro.mapping_start = static_cast<std::uint32_t>(
                align_down(relro_start, page_size));
            if (!align_up(relro_end, page_size, relro.mapping_end)) {
                return failure(Elf32LoadError::RelroSegmentAddressOverflow);
            }

            plan.relro_segments.push_back(relro);
            continue;
        }

        if (program_type != kProgramTypeLoad) {
            continue;
        }

        Elf32LoadPlanSegment segment;
        segment.offset = detail::decode_u32_le(image, offset + 4);
        segment.virtual_address = detail::decode_u32_le(image, offset + 8);
        segment.file_size = detail::decode_u32_le(image, offset + 16);
        segment.memory_size = detail::decode_u32_le(image, offset + 20);
        const std::uint32_t flags = detail::decode_u32_le(image, offset + 24);
        segment.alignment = detail::decode_u32_le(image, offset + 28);

        if (segment.file_size > segment.memory_size) {
            return failure(Elf32LoadError::SegmentFileszExceedsMemsz);
        }
        if (static_cast<std::uint64_t>(segment.offset) + segment.file_size > image.size()) {
            return failure(Elf32LoadError::SegmentFileOutOfBounds);
        }
        if (static_cast<std::uint64_t>(segment.virtual_address) + segment.memory_size >
            kGuestAddressSpaceSize) {
            return failure(Elf32LoadError::SegmentAddressOverflow);
        }
        if (segment.alignment > 1 &&
            (!is_power_of_two(segment.alignment) ||
             (segment.virtual_address % segment.alignment) !=
                 (segment.offset % segment.alignment))) {
            return failure(Elf32LoadError::SegmentAlignmentInvalid);
        }
        if (!decode_permissions(flags, segment.permissions)) {
            return failure(Elf32LoadError::UnsupportedSegmentPermissions);
        }

        if (segment.alignment > 1) {
            plan.required_load_bias_alignment =
                std::max<std::uint64_t>(plan.required_load_bias_alignment, segment.alignment);
        }

        if (segment.memory_size != 0) {
            const std::uint64_t segment_start = segment.virtual_address;
            const std::uint64_t segment_end = segment_start + segment.memory_size;
            segment.mapping_start = static_cast<std::uint32_t>(
                align_down(segment_start, page_size));
            if (!align_up(segment_end, page_size, segment.mapping_end)) {
                return failure(Elf32LoadError::SegmentAddressOverflow);
            }
            plan.minimum_page = std::min(
                plan.minimum_page, static_cast<std::uint64_t>(segment.mapping_start));
            plan.maximum_page_end = std::max(plan.maximum_page_end, segment.mapping_end);
        }

        plan.segments.push_back(segment);
    }

    if (plan.segments.empty() || plan.minimum_page == kGuestAddressSpaceSize) {
        return failure(Elf32LoadError::NoLoadSegments);
    }

    if (plan.dynamic_segment.has_value()) {
        const Elf32LoadPlanDynamicSegment& dynamic = *plan.dynamic_segment;
        const std::uint64_t dynamic_start = dynamic.virtual_address;
        const std::uint64_t dynamic_end = dynamic_start + dynamic.memory_size;
        bool contained_in_load = false;
        const Elf32LoadPlanSegment* readable_load = nullptr;

        for (const Elf32LoadPlanSegment& segment : plan.segments) {
            if (segment.memory_size == 0) {
                continue;
            }

            const std::uint64_t load_start = segment.virtual_address;
            const std::uint64_t load_end = load_start + segment.memory_size;
            if (dynamic_start >= load_start && dynamic_end <= load_end) {
                contained_in_load = true;
                if (memory::has_permission(segment.permissions, memory::MemoryPermission::Read)) {
                    readable_load = &segment;
                    break;
                }
            }
        }

        if (!contained_in_load) {
            return failure(Elf32LoadError::DynamicSegmentOutsideLoad);
        }
        if (readable_load == nullptr) {
            return failure(Elf32LoadError::DynamicSegmentNotReadable);
        }

        if (dynamic.file_size != 0) {
            const std::uint64_t load_delta = dynamic_start - readable_load->virtual_address;
            if (load_delta > readable_load->file_size ||
                dynamic.file_size >
                    static_cast<std::uint64_t>(readable_load->file_size) - load_delta ||
                static_cast<std::uint64_t>(readable_load->offset) + load_delta !=
                    dynamic.offset) {
                return failure(Elf32LoadError::DynamicSegmentFileMappingMismatch);
            }
        }
    }

    std::vector<std::size_t> order;
    order.reserve(plan.segments.size());
    for (std::size_t index = 0; index < plan.segments.size(); ++index) {
        if (plan.segments[index].memory_size != 0) {
            order.push_back(index);
        }
    }

    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        return plan.segments[lhs].mapping_start < plan.segments[rhs].mapping_start;
    });
    for (std::size_t index = 1; index < order.size(); ++index) {
        const Elf32LoadPlanSegment& previous = plan.segments[order[index - 1]];
        const Elf32LoadPlanSegment& current = plan.segments[order[index]];
        if (static_cast<std::uint64_t>(current.mapping_start) < previous.mapping_end) {
            return failure(Elf32LoadError::SegmentPageOverlap);
        }
    }

    for (const Elf32LoadPlanRelroSegment& relro : plan.relro_segments) {
        for (std::uint64_t page = relro.mapping_start;
             page < relro.mapping_end;
             page += page_size) {
            const Elf32LoadPlanSegment* containing_load = nullptr;
            for (const Elf32LoadPlanSegment& segment : plan.segments) {
                if (segment.memory_size == 0) {
                    continue;
                }

                if (page >= segment.mapping_start &&
                    page + page_size <= segment.mapping_end) {
                    containing_load = &segment;
                    break;
                }
            }

            if (containing_load == nullptr) {
                return failure(Elf32LoadError::RelroSegmentOutsideLoad);
            }
            if (!memory::has_permission(
                    containing_load->permissions, memory::MemoryPermission::Read)) {
                return failure(Elf32LoadError::RelroSegmentNotReadable);
            }
        }
    }

    Elf32LoadPlanResult result;
    result.plan = std::move(plan);
    return result;
}

}  // namespace liba32android::elf
