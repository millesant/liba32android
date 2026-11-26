#include "elf/elf32_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "elf/elf32_load_plan.h"

namespace liba32android::elf {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;

struct PlannedSegment {
    const Elf32LoadPlanSegment* raw{};
    std::uint32_t guest_address{};
    std::uint32_t mapping_start{};
    std::uint64_t mapping_end{};
};

[[nodiscard]] Elf32LoadResult failure(Elf32LoadError error) {
    Elf32LoadResult result;
    result.error = error;
    return result;
}

}  // namespace

Elf32LoadResult load_elf32(memory::MappedGuestMemory& memory,
                           std::span<const std::uint8_t> image,
                           const Elf32LoadOptions& options) {
    const auto plan_result = plan_elf32_load(memory, image);
    if (!plan_result) {
        return failure(plan_result.error);
    }

    const Elf32LoadPlan& plan = plan_result.plan;
    const std::uint64_t page_size = memory.page_size();

    std::uint32_t load_bias = 0;
    if (plan.type == Elf32ImageType::Dynamic) {
        if (!options.dynamic_base.has_value()) {
            return failure(Elf32LoadError::DynamicBaseRequired);
        }

        const std::uint64_t dynamic_base = *options.dynamic_base;
        if ((dynamic_base % page_size) != 0) {
            return failure(Elf32LoadError::DynamicBaseUnaligned);
        }
        if (dynamic_base < plan.minimum_page) {
            return failure(Elf32LoadError::LoadBiasOverflow);
        }

        const std::uint64_t bias = dynamic_base - plan.minimum_page;
        if (bias > std::numeric_limits<std::uint32_t>::max() ||
            plan.maximum_page_end > kGuestAddressSpaceSize - bias) {
            return failure(Elf32LoadError::LoadBiasOverflow);
        }
        if (plan.required_load_bias_alignment > 1 &&
            (bias % plan.required_load_bias_alignment) != 0) {
            return failure(Elf32LoadError::DynamicBaseUnaligned);
        }

        load_bias = static_cast<std::uint32_t>(bias);
    }

    std::uint32_t loaded_entry = 0;
    if (plan.entry != 0) {
        const std::uint64_t biased_entry =
            static_cast<std::uint64_t>(plan.entry) + load_bias;
        if (biased_entry > std::numeric_limits<std::uint32_t>::max()) {
            return failure(Elf32LoadError::EntryAddressOverflow);
        }
        loaded_entry = static_cast<std::uint32_t>(biased_entry);
    }

    std::optional<Elf32DynamicSegment> loaded_dynamic_segment;
    if (plan.dynamic_segment.has_value()) {
        const Elf32LoadPlanDynamicSegment& dynamic = *plan.dynamic_segment;
        const std::uint64_t biased_dynamic_start =
            static_cast<std::uint64_t>(dynamic.virtual_address) + load_bias;
        const std::uint64_t biased_dynamic_end =
            biased_dynamic_start + dynamic.memory_size;
        if (biased_dynamic_start > std::numeric_limits<std::uint32_t>::max() ||
            biased_dynamic_end > kGuestAddressSpaceSize) {
            return failure(Elf32LoadError::DynamicSegmentAddressOverflow);
        }

        loaded_dynamic_segment = Elf32DynamicSegment{
            .guest_address = static_cast<std::uint32_t>(biased_dynamic_start),
            .file_size = dynamic.file_size,
            .memory_size = dynamic.memory_size,
        };
    }

    std::vector<Elf32RelroSegment> loaded_relro_segments;
    loaded_relro_segments.reserve(plan.relro_segments.size());
    for (const Elf32LoadPlanRelroSegment& raw : plan.relro_segments) {
        const std::uint64_t guest_address =
            static_cast<std::uint64_t>(raw.virtual_address) + load_bias;
        const std::uint64_t mapping_start =
            static_cast<std::uint64_t>(raw.mapping_start) + load_bias;
        const std::uint64_t mapping_end = raw.mapping_end + load_bias;
        if (guest_address > std::numeric_limits<std::uint32_t>::max() ||
            mapping_start > std::numeric_limits<std::uint32_t>::max() ||
            mapping_end > kGuestAddressSpaceSize) {
            return failure(Elf32LoadError::RelroSegmentAddressOverflow);
        }

        loaded_relro_segments.push_back({
            .guest_address = static_cast<std::uint32_t>(guest_address),
            .memory_size = raw.memory_size,
            .mapping_start = static_cast<std::uint32_t>(mapping_start),
            .mapping_size = mapping_end - mapping_start,
        });
    }

    std::vector<PlannedSegment> planned_segments;
    planned_segments.reserve(plan.segments.size());
    for (const Elf32LoadPlanSegment& raw : plan.segments) {
        if (raw.memory_size == 0) {
            continue;
        }

        const std::uint64_t guest_address =
            static_cast<std::uint64_t>(raw.virtual_address) + load_bias;
        const std::uint64_t mapping_start =
            static_cast<std::uint64_t>(raw.mapping_start) + load_bias;
        const std::uint64_t mapping_end = raw.mapping_end + load_bias;
        if (guest_address > std::numeric_limits<std::uint32_t>::max() ||
            mapping_start > std::numeric_limits<std::uint32_t>::max() ||
            mapping_end > kGuestAddressSpaceSize) {
            return failure(Elf32LoadError::SegmentAddressOverflow);
        }

        planned_segments.push_back({
            .raw = &raw,
            .guest_address = static_cast<std::uint32_t>(guest_address),
            .mapping_start = static_cast<std::uint32_t>(mapping_start),
            .mapping_end = mapping_end,
        });
    }

    for (const PlannedSegment& planned : planned_segments) {
        for (std::uint64_t page = planned.mapping_start;
             page < planned.mapping_end;
             page += page_size) {
            if (memory.is_mapped(static_cast<std::uint32_t>(page))) {
                return failure(Elf32LoadError::AddressConflict);
            }
        }
    }

    struct MappedRange {
        std::uint32_t start{};
        std::size_t length{};
    };
    std::vector<MappedRange> mapped_ranges;
    mapped_ranges.reserve(planned_segments.size());

    const auto rollback = [&] {
        for (auto it = mapped_ranges.rbegin(); it != mapped_ranges.rend(); ++it) {
            (void)memory.unmap(it->start, it->length);
        }
    };

    const auto initialization_permissions =
        memory::MemoryPermission::Read | memory::MemoryPermission::Write;
    for (const PlannedSegment& planned : planned_segments) {
        const auto mapping_size = static_cast<std::size_t>(
            planned.mapping_end - static_cast<std::uint64_t>(planned.mapping_start));
        if (!memory.map(planned.mapping_start, mapping_size, initialization_permissions)) {
            rollback();
            return failure(Elf32LoadError::MapFailed);
        }
        mapped_ranges.push_back({planned.mapping_start, mapping_size});
    }

    constexpr std::size_t kZeroChunkSize = 4096;
    const std::array<std::uint8_t, kZeroChunkSize> zeroes{};
    for (const PlannedSegment& planned : planned_segments) {
        const Elf32LoadPlanSegment& raw = *planned.raw;
        if (raw.file_size != 0) {
            const auto file_bytes = image.subspan(raw.offset, raw.file_size);
            if (!memory.write(planned.guest_address, file_bytes)) {
                rollback();
                return failure(Elf32LoadError::WriteFailed);
            }
        }

        std::uint64_t zero_address =
            static_cast<std::uint64_t>(planned.guest_address) + raw.file_size;
        std::uint64_t remaining =
            static_cast<std::uint64_t>(raw.memory_size) - raw.file_size;
        while (remaining != 0) {
            const std::size_t chunk =
                static_cast<std::size_t>(std::min<std::uint64_t>(remaining, zeroes.size()));
            if (!memory.write(
                    static_cast<std::uint32_t>(zero_address),
                    std::span<const std::uint8_t>{zeroes.data(), chunk})) {
                rollback();
                return failure(Elf32LoadError::WriteFailed);
            }
            zero_address += chunk;
            remaining -= chunk;
        }
    }

    for (const PlannedSegment& planned : planned_segments) {
        const auto mapping_size = static_cast<std::size_t>(
            planned.mapping_end - static_cast<std::uint64_t>(planned.mapping_start));
        if (!memory.protect(
                planned.mapping_start, mapping_size, planned.raw->permissions)) {
            rollback();
            return failure(Elf32LoadError::ProtectFailed);
        }
    }

    Elf32LoadResult result;
    result.load_bias = load_bias;
    result.entry = loaded_entry;
    result.dynamic_segment = loaded_dynamic_segment;
    result.relro_segments = loaded_relro_segments;
    result.segments.reserve(planned_segments.size());
    for (const PlannedSegment& planned : planned_segments) {
        const Elf32LoadPlanSegment& raw = *planned.raw;
        result.segments.push_back({
            .guest_address = planned.guest_address,
            .file_size = raw.file_size,
            .memory_size = raw.memory_size,
            .mapping_start = planned.mapping_start,
            .mapping_size =
                planned.mapping_end - static_cast<std::uint64_t>(planned.mapping_start),
            .permissions = raw.permissions,
        });
    }
    return result;
}

const char* to_string(Elf32LoadError error) noexcept {
    switch (error) {
    case Elf32LoadError::None: return "none";
    case Elf32LoadError::TruncatedHeader: return "truncated_header";
    case Elf32LoadError::BadMagic: return "bad_magic";
    case Elf32LoadError::UnsupportedClass: return "unsupported_class";
    case Elf32LoadError::UnsupportedEndian: return "unsupported_endian";
    case Elf32LoadError::UnsupportedIdentVersion: return "unsupported_ident_version";
    case Elf32LoadError::UnsupportedElfVersion: return "unsupported_elf_version";
    case Elf32LoadError::UnsupportedType: return "unsupported_type";
    case Elf32LoadError::UnsupportedMachine: return "unsupported_machine";
    case Elf32LoadError::InvalidHeaderSize: return "invalid_header_size";
    case Elf32LoadError::InvalidProgramHeaderSize: return "invalid_program_header_size";
    case Elf32LoadError::ProgramHeaderTableOutOfBounds:
        return "program_header_table_out_of_bounds";
    case Elf32LoadError::NoLoadSegments: return "no_load_segments";
    case Elf32LoadError::SegmentFileszExceedsMemsz:
        return "segment_filesz_exceeds_memsz";
    case Elf32LoadError::SegmentFileOutOfBounds:
        return "segment_file_out_of_bounds";
    case Elf32LoadError::SegmentAddressOverflow:
        return "segment_address_overflow";
    case Elf32LoadError::SegmentAlignmentInvalid:
        return "segment_alignment_invalid";
    case Elf32LoadError::UnsupportedSegmentPermissions:
        return "unsupported_segment_permissions";
    case Elf32LoadError::DynamicBaseRequired: return "dynamic_base_required";
    case Elf32LoadError::DynamicBaseUnaligned: return "dynamic_base_unaligned";
    case Elf32LoadError::LoadBiasOverflow: return "load_bias_overflow";
    case Elf32LoadError::EntryAddressOverflow: return "entry_address_overflow";
    case Elf32LoadError::MultipleDynamicSegments:
        return "multiple_dynamic_segments";
    case Elf32LoadError::DynamicSegmentEmpty:
        return "dynamic_segment_empty";
    case Elf32LoadError::DynamicSegmentFileszExceedsMemsz:
        return "dynamic_segment_filesz_exceeds_memsz";
    case Elf32LoadError::DynamicSegmentFileOutOfBounds:
        return "dynamic_segment_file_out_of_bounds";
    case Elf32LoadError::DynamicSegmentAddressOverflow:
        return "dynamic_segment_address_overflow";
    case Elf32LoadError::DynamicSegmentOutsideLoad:
        return "dynamic_segment_outside_load";
    case Elf32LoadError::DynamicSegmentNotReadable:
        return "dynamic_segment_not_readable";
    case Elf32LoadError::DynamicSegmentFileMappingMismatch:
        return "dynamic_segment_file_mapping_mismatch";
    case Elf32LoadError::RelroSegmentEmpty:
        return "relro_segment_empty";
    case Elf32LoadError::RelroSegmentAddressOverflow:
        return "relro_segment_address_overflow";
    case Elf32LoadError::RelroSegmentOutsideLoad:
        return "relro_segment_outside_load";
    case Elf32LoadError::RelroSegmentNotReadable:
        return "relro_segment_not_readable";
    case Elf32LoadError::SegmentPageOverlap:
        return "segment_page_overlap";
    case Elf32LoadError::AddressConflict: return "address_conflict";
    case Elf32LoadError::MapFailed: return "map_failed";
    case Elf32LoadError::WriteFailed: return "write_failed";
    case Elf32LoadError::ProtectFailed: return "protect_failed";
    }
    return "unknown";
}

}  // namespace liba32android::elf
