#pragma once

#define LIBA32ANDROID_A32_ANDROID_LOG_WRITE_SHIM_SVC 0xA0
#define LIBA32ANDROID_A32_ANDROID_LOG_PRINT_SHIM_SVC 0x133
#define LIBA32ANDROID_A32_ANDROID_LOG_VPRINT_SHIM_SVC 0x134

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string_view>

#include "elf/elf32_dependency_resolver.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32AndroidLogWriteShimSvcImmediate =
    LIBA32ANDROID_A32_ANDROID_LOG_WRITE_SHIM_SVC;
inline constexpr std::uint32_t kA32AndroidLogPrintShimSvcImmediate =
    LIBA32ANDROID_A32_ANDROID_LOG_PRINT_SHIM_SVC;
inline constexpr std::uint32_t kA32AndroidLogVprintShimSvcImmediate =
    LIBA32ANDROID_A32_ANDROID_LOG_VPRINT_SHIM_SVC;
inline constexpr std::string_view kA32AndroidLogShimSoname = "liblog.so";
inline constexpr std::string_view kA32AndroidLogShimIdentity =
    "liba32android-compat-liblog";

[[nodiscard]] inline elf::Elf32DependencyCatalogEntry
make_a32_android_log_shim_catalog_entry(
    std::span<const std::uint8_t> image) noexcept {
    return elf::Elf32DependencyCatalogEntry{
        .requested_name = kA32AndroidLogShimSoname,
        .identity = kA32AndroidLogShimIdentity,
        .image = image,
    };
}

}  // namespace liba32android::compat

#endif
