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
#define LIBA32ANDROID_A32_LIBM_ACOSF_SVC 0x180
#define LIBA32ANDROID_A32_LIBM_ATAN_SVC 0x181
#define LIBA32ANDROID_A32_LIBM_ATAN2F_SVC 0x182
#define LIBA32ANDROID_A32_LIBM_ATANF_SVC 0x183
#define LIBA32ANDROID_A32_LIBM_CBRT_SVC 0x184
#define LIBA32ANDROID_A32_LIBM_CBRTF_SVC 0x185
#define LIBA32ANDROID_A32_LIBM_CEIL_SVC 0x186
#define LIBA32ANDROID_A32_LIBM_CEILF_SVC 0x187
#define LIBA32ANDROID_A32_LIBM_COSH_SVC 0x188
#define LIBA32ANDROID_A32_LIBM_EXP2_SVC 0x189
#define LIBA32ANDROID_A32_LIBM_EXP2F_SVC 0x18A
#define LIBA32ANDROID_A32_LIBM_EXPF_SVC 0x18B
#define LIBA32ANDROID_A32_LIBM_EXPM1_SVC 0x18C
#define LIBA32ANDROID_A32_LIBM_FABS_SVC 0x18D
#define LIBA32ANDROID_A32_LIBM_FLOORF_SVC 0x18E
#define LIBA32ANDROID_A32_LIBM_FMAX_SVC 0x18F
#define LIBA32ANDROID_A32_LIBM_FMAXF_SVC 0x190
#define LIBA32ANDROID_A32_LIBM_FMINF_SVC 0x191
#define LIBA32ANDROID_A32_LIBM_FMOD_SVC 0x192
#define LIBA32ANDROID_A32_LIBM_FMODF_SVC 0x193
#define LIBA32ANDROID_A32_LIBM_FREXPF_SVC 0x194
#define LIBA32ANDROID_A32_LIBM_HYPOT_SVC 0x195
#define LIBA32ANDROID_A32_LIBM_HYPOTF_SVC 0x196
#define LIBA32ANDROID_A32_LIBM_LDEXPF_SVC 0x197
#define LIBA32ANDROID_A32_LIBM_LLRINT_SVC 0x198
#define LIBA32ANDROID_A32_LIBM_LLRINTF_SVC 0x199
#define LIBA32ANDROID_A32_LIBM_LLROUND_SVC 0x19A
#define LIBA32ANDROID_A32_LIBM_LLROUNDF_SVC 0x19B
#define LIBA32ANDROID_A32_LIBM_LOG1P_SVC 0x19C
#define LIBA32ANDROID_A32_LIBM_LOGF_SVC 0x19D
#define LIBA32ANDROID_A32_LIBM_LRINT_SVC 0x19E
#define LIBA32ANDROID_A32_LIBM_LRINTF_SVC 0x19F
#define LIBA32ANDROID_A32_LIBM_LROUND_SVC 0x1A0
#define LIBA32ANDROID_A32_LIBM_LROUNDF_SVC 0x1A1
#define LIBA32ANDROID_A32_LIBM_MODF_SVC 0x1A2
#define LIBA32ANDROID_A32_LIBM_MODFF_SVC 0x1A3
#define LIBA32ANDROID_A32_LIBM_NANF_SVC 0x1A4
#define LIBA32ANDROID_A32_LIBM_RINT_SVC 0x1A5
#define LIBA32ANDROID_A32_LIBM_RINTF_SVC 0x1A6
#define LIBA32ANDROID_A32_LIBM_ROUND_SVC 0x1A7
#define LIBA32ANDROID_A32_LIBM_ROUNDF_SVC 0x1A8
#define LIBA32ANDROID_A32_LIBM_SCALBN_SVC 0x1A9
#define LIBA32ANDROID_A32_LIBM_SINCOS_SVC 0x1AA
#define LIBA32ANDROID_A32_LIBM_SINCOSF_SVC 0x1AB
#define LIBA32ANDROID_A32_LIBM_SINH_SVC 0x1AC
#define LIBA32ANDROID_A32_LIBM_TANF_SVC 0x1AD
#define LIBA32ANDROID_A32_LIBM_TANH_SVC 0x1AE
#define LIBA32ANDROID_A32_LIBM_TRUNC_SVC 0x1AF
#define LIBA32ANDROID_A32_LIBM_TRUNCF_SVC 0x1B0

#ifdef __cplusplus

#include <array>
#include <cstddef>
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
inline constexpr std::uint32_t kA32LibmAcosfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ACOSF_SVC;
inline constexpr std::uint32_t kA32LibmAtanSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ATAN_SVC;
inline constexpr std::uint32_t kA32LibmAtan2fSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ATAN2F_SVC;
inline constexpr std::uint32_t kA32LibmAtanfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ATANF_SVC;
inline constexpr std::uint32_t kA32LibmCbrtSvcImmediate =
    LIBA32ANDROID_A32_LIBM_CBRT_SVC;
inline constexpr std::uint32_t kA32LibmCbrtfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_CBRTF_SVC;
inline constexpr std::uint32_t kA32LibmCeilSvcImmediate =
    LIBA32ANDROID_A32_LIBM_CEIL_SVC;
inline constexpr std::uint32_t kA32LibmCeilfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_CEILF_SVC;
inline constexpr std::uint32_t kA32LibmCoshSvcImmediate =
    LIBA32ANDROID_A32_LIBM_COSH_SVC;
inline constexpr std::uint32_t kA32LibmExp2SvcImmediate =
    LIBA32ANDROID_A32_LIBM_EXP2_SVC;
inline constexpr std::uint32_t kA32LibmExp2fSvcImmediate =
    LIBA32ANDROID_A32_LIBM_EXP2F_SVC;
inline constexpr std::uint32_t kA32LibmExpfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_EXPF_SVC;
inline constexpr std::uint32_t kA32LibmExpm1SvcImmediate =
    LIBA32ANDROID_A32_LIBM_EXPM1_SVC;
inline constexpr std::uint32_t kA32LibmFabsSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FABS_SVC;
inline constexpr std::uint32_t kA32LibmFloorfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FLOORF_SVC;
inline constexpr std::uint32_t kA32LibmFmaxSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FMAX_SVC;
inline constexpr std::uint32_t kA32LibmFmaxfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FMAXF_SVC;
inline constexpr std::uint32_t kA32LibmFminfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FMINF_SVC;
inline constexpr std::uint32_t kA32LibmFmodSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FMOD_SVC;
inline constexpr std::uint32_t kA32LibmFmodfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FMODF_SVC;
inline constexpr std::uint32_t kA32LibmFrexpfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_FREXPF_SVC;
inline constexpr std::uint32_t kA32LibmHypotSvcImmediate =
    LIBA32ANDROID_A32_LIBM_HYPOT_SVC;
inline constexpr std::uint32_t kA32LibmHypotfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_HYPOTF_SVC;
inline constexpr std::uint32_t kA32LibmLdexpfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LDEXPF_SVC;
inline constexpr std::uint32_t kA32LibmLlrintSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LLRINT_SVC;
inline constexpr std::uint32_t kA32LibmLlrintfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LLRINTF_SVC;
inline constexpr std::uint32_t kA32LibmLlroundSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LLROUND_SVC;
inline constexpr std::uint32_t kA32LibmLlroundfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LLROUNDF_SVC;
inline constexpr std::uint32_t kA32LibmLog1pSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LOG1P_SVC;
inline constexpr std::uint32_t kA32LibmLogfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LOGF_SVC;
inline constexpr std::uint32_t kA32LibmLrintSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LRINT_SVC;
inline constexpr std::uint32_t kA32LibmLrintfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LRINTF_SVC;
inline constexpr std::uint32_t kA32LibmLroundSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LROUND_SVC;
inline constexpr std::uint32_t kA32LibmLroundfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_LROUNDF_SVC;
inline constexpr std::uint32_t kA32LibmModfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_MODF_SVC;
inline constexpr std::uint32_t kA32LibmModffSvcImmediate =
    LIBA32ANDROID_A32_LIBM_MODFF_SVC;
inline constexpr std::uint32_t kA32LibmNanfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_NANF_SVC;
inline constexpr std::uint32_t kA32LibmRintSvcImmediate =
    LIBA32ANDROID_A32_LIBM_RINT_SVC;
inline constexpr std::uint32_t kA32LibmRintfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_RINTF_SVC;
inline constexpr std::uint32_t kA32LibmRoundSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ROUND_SVC;
inline constexpr std::uint32_t kA32LibmRoundfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_ROUNDF_SVC;
inline constexpr std::uint32_t kA32LibmScalbnSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SCALBN_SVC;
inline constexpr std::uint32_t kA32LibmSincosSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SINCOS_SVC;
inline constexpr std::uint32_t kA32LibmSincosfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SINCOSF_SVC;
inline constexpr std::uint32_t kA32LibmSinhSvcImmediate =
    LIBA32ANDROID_A32_LIBM_SINH_SVC;
inline constexpr std::uint32_t kA32LibmTanfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_TANF_SVC;
inline constexpr std::uint32_t kA32LibmTanhSvcImmediate =
    LIBA32ANDROID_A32_LIBM_TANH_SVC;
inline constexpr std::uint32_t kA32LibmTruncSvcImmediate =
    LIBA32ANDROID_A32_LIBM_TRUNC_SVC;
inline constexpr std::uint32_t kA32LibmTruncfSvcImmediate =
    LIBA32ANDROID_A32_LIBM_TRUNCF_SVC;

inline constexpr std::string_view kA32LibmShimSoname = "libm.so";
inline constexpr std::string_view kA32LibmShimIdentity =
    "liba32android-compat-libm";

struct A32LibmOptions {
    // nanf() is the only selected libm call that reads a guest string.
    std::size_t max_nan_tag_bytes{64U};
};

class A32LibmService final : public runtime::A32HostServiceHandler {
public:
    explicit A32LibmService(A32LibmOptions options = {}) noexcept
        : options_(options) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    A32LibmOptions options_;
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
