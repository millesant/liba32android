#pragma once

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string_view>

#include "elf/elf32_dependency_resolver.h"

namespace liba32android::compat {

inline constexpr std::string_view kA32LibcMemoryStringShimSoname = "libc.so";
inline constexpr std::string_view kA32LibcMemoryStringShimIdentity =
    "liba32android-compat-libc-memory-string";

[[nodiscard]] inline elf::Elf32DependencyCatalogEntry
make_a32_libc_memory_string_shim_catalog_entry(
    std::span<const std::uint8_t> image) noexcept {
    return elf::Elf32DependencyCatalogEntry{
        .requested_name = kA32LibcMemoryStringShimSoname,
        .identity = kA32LibcMemoryStringShimIdentity,
        .image = image,
    };
}

}  // namespace liba32android::compat

#endif
