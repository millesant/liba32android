#pragma once

#define LIBA32ANDROID_A32_LIBM_ACOS_SVC 0xC1
#define LIBA32ANDROID_A32_LIBM_ASIN_SVC 0xC2
#define LIBA32ANDROID_A32_LIBM_ATAN2_SVC 0xC3
#define LIBA32ANDROID_A32_LIBM_COS_SVC 0xC4
#define LIBA32ANDROID_A32_LIBM_COSF_SVC 0xC5
#define LIBA32ANDROID_A32_LIBM_EXP_SVC 0xC6
#define LIBA32ANDROID_A32_LIBM_FLOOR_SVC 0xC7
#define LIBA32ANDROID_A32_LIBM_FREXP_SVC 0xC8
#define LIBA32ANDROID_A32_LIBM_LDEXP_SVC 0xC9
#define LIBA32ANDROID_A32_LIBM_LOG_SVC 0xCA
#define LIBA32ANDROID_A32_LIBM_LOG10_SVC 0xCB
#define LIBA32ANDROID_A32_LIBM_LOG10F_SVC 0xCC
#define LIBA32ANDROID_A32_LIBM_POW_SVC 0xCD
#define LIBA32ANDROID_A32_LIBM_POWF_SVC 0xCE
#define LIBA32ANDROID_A32_LIBM_SIN_SVC 0xCF
#define LIBA32ANDROID_A32_LIBM_SINF_SVC 0xD0
#define LIBA32ANDROID_A32_LIBM_TAN_SVC 0xD1

#ifdef __cplusplus

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "elf/elf32_dependency_resolver.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32LibmAcosSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ACOS_SVC;
inline constexpr std::uint32_t kA32LibmAsinSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ASIN_SVC;
inline constexpr std::uint32_t kA32LibmAtan2SvcImmediate =
    LIBA32ANDROID_A32_LIBM_ATAN2_SVC;
inline constexpr std::uint32_t kA32LibmCosSvcImmediate =
    LIBA32ANDROID_A32_LIBM_COS_SVC;
inline constexpr std::uint32_t kA32LibmCosfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_COSF_SVC;
inline constexpr std::uint32_t kA32LibmExpSvcImmediate =
    LIBA32ANDROID_A32_LIBM_EXP_SVC;
inline constexpr std::uint32_t kA32LibmFloorSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FLOOR_SVC;
inline constexpr std::uint32_t kA32LibmFrexpSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FREXP_SVC;
inline constexpr std::uint32_t kA32LibmLdexpSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LDEXP_SVC;
inline constexpr std::uint32_t kA32LibmLogSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LOG_SVC;
inline constexpr std::uint32_t kA32LibmLog10SvcImmediate =
    LIBA32ANDROID_A32_LIBM_LOG10_SVC;
inline constexpr std::uint32_t kA32LibmLog10fSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LOG10F_SVC;
inline constexpr std::uint32_t kA32LibmPowSvcImmediate =
    LIBA32ANDROID_A32_LIBM_POW_SVC;
inline constexpr std::uint32_t kA32LibmPowfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_POWF_SVC;
inline constexpr std::uint32_t kA32LibmSinSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SIN_SVC;
inline constexpr std::uint32_t kA32LibmSinfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SINF_SVC;
inline constexpr std::uint32_t kA32LibmTanSvcImmediate =
    LIBA32ANDROID_A32_LIBM_TAN_SVC;

inline constexpr std::string_view kA32LibmShimSoname = "libm.so";
inline constexpr std::string_view kA32LibmShimIdentity =
    "liba32android-compat-libm";

class A32LibmService final : public runtime::A32HostServiceHandler {
public:
    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;
};

[[nodiscard]] inline elf::Elf32DependencyCatalogEntry
make_a32_libm_shim_catalog_entry(
    std::span<const std::uint8_t> image) noexcept {
    return elf::Elf32DependencyCatalogEntry{
        .requested_name = kA32LibmShimSoname,
        .identity = kA32LibmShimIdentity,
        .image = image,
    };
}

}  // namespace liba32android::compat

#endif
