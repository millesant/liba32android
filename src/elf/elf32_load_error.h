#pragma once

#include <cstdint>

namespace liba32android::elf {

enum class Elf32LoadError : std::uint8_t {
    None = 0,
    TruncatedHeader,
    BadMagic,
    UnsupportedClass,
    UnsupportedEndian,
    UnsupportedIdentVersion,
    UnsupportedElfVersion,
    UnsupportedType,
    UnsupportedMachine,
    InvalidHeaderSize,
    InvalidProgramHeaderSize,
    ProgramHeaderTableOutOfBounds,
    NoLoadSegments,
    SegmentFileszExceedsMemsz,
    SegmentFileOutOfBounds,
    SegmentAddressOverflow,
    SegmentAlignmentInvalid,
    UnsupportedSegmentPermissions,
    DynamicBaseRequired,
    DynamicBaseUnaligned,
    LoadBiasOverflow,
    EntryAddressOverflow,
    MultipleDynamicSegments,
    DynamicSegmentEmpty,
    DynamicSegmentFileszExceedsMemsz,
    DynamicSegmentFileOutOfBounds,
    DynamicSegmentAddressOverflow,
    DynamicSegmentOutsideLoad,
    DynamicSegmentNotReadable,
    DynamicSegmentFileMappingMismatch,
    RelroSegmentEmpty,
    RelroSegmentAddressOverflow,
    RelroSegmentOutsideLoad,
    RelroSegmentNotReadable,
    SegmentPageOverlap,
    AddressConflict,
    MapFailed,
    WriteFailed,
    ProtectFailed,
};

[[nodiscard]] const char* to_string(Elf32LoadError error) noexcept;

}  // namespace liba32android::elf
