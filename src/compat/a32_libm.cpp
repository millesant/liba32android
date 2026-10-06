#include "compat/a32_libm.h"

#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

static_assert(sizeof(float) == 4U);
static_assert(sizeof(double) == 8U);
static_assert(sizeof(std::int32_t) == 4U);
static_assert(sizeof(std::int64_t) == 8U);

[[nodiscard]] bool is_libm_svc(std::uint32_t svc) noexcept {
    return (svc >= kA32LibmAcosSvcImmediate &&
            svc <= kA32LibmTanSvcImmediate) ||
           (svc >= kA32LibmAcosfSvcImmediate &&
            svc <= kA32LibmTruncfSvcImmediate);
}

[[nodiscard]] double read_double(
    const std::array<std::uint32_t, 16>& regs,
    std::size_t first) noexcept {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(regs[first]) |
        (static_cast<std::uint64_t>(regs[first + 1U]) << 32U);
    return std::bit_cast<double>(bits);
}

void write_double(std::array<std::uint32_t, 16>& regs, double value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    regs[0] = static_cast<std::uint32_t>(bits);
    regs[1] = static_cast<std::uint32_t>(bits >> 32U);
}

[[nodiscard]] float read_float(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

void write_float(std::array<std::uint32_t, 16>& regs, float value) noexcept {
    regs[0] = std::bit_cast<std::uint32_t>(value);
}

void write_i64(
    std::array<std::uint32_t, 16>& regs,
    std::int64_t value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    regs[0] = static_cast<std::uint32_t>(bits);
    regs[1] = static_cast<std::uint32_t>(bits >> 32U);
}

template <typename Function>
[[nodiscard]] auto invoke_preserving_host_math_state(Function&& function) {
    const int saved_errno = errno;
    std::fenv_t saved_environment{};
    const bool have_environment = std::fegetenv(&saved_environment) == 0;
    auto result = function();
    if (have_environment) {
        static_cast<void>(std::fesetenv(&saved_environment));
    }
    errno = saved_errno;
    return result;
}

[[nodiscard]] bool write_i32_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::int32_t value) {
    if (address == 0U ||
        address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(bits),
        static_cast<std::uint8_t>(bits >> 8U),
        static_cast<std::uint8_t>(bits >> 16U),
        static_cast<std::uint8_t>(bits >> 24U),
    }};
    return memory.write(address, bytes);
}

[[nodiscard]] bool write_f32_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    float value) {
    if (address == 0U ||
        address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(bits),
        static_cast<std::uint8_t>(bits >> 8U),
        static_cast<std::uint8_t>(bits >> 16U),
        static_cast<std::uint8_t>(bits >> 24U),
    }};
    return memory.write(address, bytes);
}

[[nodiscard]] bool write_f64_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    double value) {
    if (address == 0U ||
        address > std::numeric_limits<std::uint32_t>::max() - 7U) {
        return false;
    }
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(bits >> (i * 8U));
    }
    return memory.write(address, bytes);
}

[[nodiscard]] bool read_guest_c_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t max_payload_bytes,
    std::string& output) {
    if (address == 0U) return false;
    output.clear();
    for (std::size_t offset = 0;; ++offset) {
        if (offset >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max() - address)) {
            return false;
        }
        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(
                address + static_cast<std::uint32_t>(offset), byte)) {
            return false;
        }
        if (byte[0] == 0U) return true;
        if (offset == max_payload_bytes) return false;
        output.push_back(static_cast<char>(byte[0]));
    }
}

}  // namespace

runtime::A32HostServiceDisposition A32LibmService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_libm_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }

    switch (svc_immediate) {
    case kA32LibmAcosSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::acos(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAsinSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::asin(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAtanSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::atan(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCbrtSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::cbrt(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCeilSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::ceil(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCosSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::cos(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCoshSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::cosh(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmExpSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::exp(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmExp2SvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::exp2(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmExpm1SvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::expm1(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFabsSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::fabs(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFloorSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::floor(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLogSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::log(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLog10SvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::log10(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLog1pSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::log1p(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmRintSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::rint(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmRoundSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::round(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmSinSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::sin(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmSinhSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::sinh(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmTanSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::tan(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmTanhSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::tanh(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmTruncSvcImmediate: {
        const double x = read_double(regs, 0U);
        write_double(regs, invoke_preserving_host_math_state(
            [x] { return std::trunc(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAcosfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::acos(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAtanfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::atan(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCbrtfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::cbrt(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCeilfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::ceil(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmCosfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::cos(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmExp2fSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::exp2(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmExpfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::exp(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFloorfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::floor(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLog10fSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::log10(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLogfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::log(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmRintfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::rint(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmRoundfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::round(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmSinfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::sin(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmTanfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::tan(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmTruncfSvcImmediate: {
        const float x = read_float(regs[0]);
        write_float(regs, invoke_preserving_host_math_state(
            [x] { return std::trunc(x); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAtan2SvcImmediate: {
        const double first = read_double(regs, 0U);
        const double second = read_double(regs, 2U);
        write_double(regs, invoke_preserving_host_math_state(
            [first, second] { return std::atan2(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFmaxSvcImmediate: {
        const double first = read_double(regs, 0U);
        const double second = read_double(regs, 2U);
        write_double(regs, invoke_preserving_host_math_state(
            [first, second] { return std::fmax(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFmodSvcImmediate: {
        const double first = read_double(regs, 0U);
        const double second = read_double(regs, 2U);
        write_double(regs, invoke_preserving_host_math_state(
            [first, second] { return std::fmod(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmHypotSvcImmediate: {
        const double first = read_double(regs, 0U);
        const double second = read_double(regs, 2U);
        write_double(regs, invoke_preserving_host_math_state(
            [first, second] { return std::hypot(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmPowSvcImmediate: {
        const double first = read_double(regs, 0U);
        const double second = read_double(regs, 2U);
        write_double(regs, invoke_preserving_host_math_state(
            [first, second] { return std::pow(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmAtan2fSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::atan2(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFmaxfSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::fmax(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFminfSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::fmin(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFmodfSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::fmod(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmHypotfSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::hypot(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmPowfSvcImmediate: {
        const float first = read_float(regs[0]);
        const float second = read_float(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [first, second] { return std::pow(first, second); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFrexpSvcImmediate: {
        const double x = read_double(regs, 0U);
        std::int32_t exponent{};
        const double result = invoke_preserving_host_math_state([&] {
            int host_exponent{};
            const double value = std::frexp(x, &host_exponent);
            exponent = static_cast<std::int32_t>(host_exponent);
            return value;
        });
        if (!write_i32_le(memory, regs[2], exponent)) {
            return A32HostServiceDisposition::Failed;
        }
        write_double(regs, result);
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmFrexpfSvcImmediate: {
        const float x = read_float(regs[0]);
        std::int32_t exponent{};
        const float result = invoke_preserving_host_math_state([&] {
            int host_exponent{};
            const float value = std::frexp(x, &host_exponent);
            exponent = static_cast<std::int32_t>(host_exponent);
            return value;
        });
        if (!write_i32_le(memory, regs[1], exponent)) {
            return A32HostServiceDisposition::Failed;
        }
        write_float(regs, result);
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLdexpSvcImmediate:
    case kA32LibmScalbnSvcImmediate: {
        const double x = read_double(regs, 0U);
        const std::int32_t exponent = std::bit_cast<std::int32_t>(regs[2]);
        const double result = invoke_preserving_host_math_state(
            [x, exponent, svc_immediate] {
                return svc_immediate == kA32LibmLdexpSvcImmediate
                    ? std::ldexp(x, static_cast<int>(exponent))
                    : std::scalbn(x, static_cast<int>(exponent));
            });
        write_double(regs, result);
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLdexpfSvcImmediate: {
        const float x = read_float(regs[0]);
        const std::int32_t exponent = std::bit_cast<std::int32_t>(regs[1]);
        write_float(regs, invoke_preserving_host_math_state(
            [x, exponent] { return std::ldexp(x, static_cast<int>(exponent)); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLlrintSvcImmediate:
    case kA32LibmLlroundSvcImmediate: {
        const double x = read_double(regs, 0U);
        const long long result = invoke_preserving_host_math_state(
            [x, svc_immediate] {
                return svc_immediate == kA32LibmLlrintSvcImmediate
                    ? std::llrint(x)
                    : std::llround(x);
            });
        write_i64(regs, static_cast<std::int64_t>(result));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLlrintfSvcImmediate:
    case kA32LibmLlroundfSvcImmediate: {
        const float x = read_float(regs[0]);
        const long long result = invoke_preserving_host_math_state(
            [x, svc_immediate] {
                return svc_immediate == kA32LibmLlrintfSvcImmediate
                    ? std::llrint(x)
                    : std::llround(x);
            });
        write_i64(regs, static_cast<std::int64_t>(result));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLrintSvcImmediate:
    case kA32LibmLroundSvcImmediate: {
        const double x = read_double(regs, 0U);
        const long result = invoke_preserving_host_math_state(
            [x, svc_immediate] {
                return svc_immediate == kA32LibmLrintSvcImmediate
                    ? std::lrint(x)
                    : std::lround(x);
            });
        regs[0] = static_cast<std::uint32_t>(static_cast<std::int64_t>(result));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmLrintfSvcImmediate:
    case kA32LibmLroundfSvcImmediate: {
        const float x = read_float(regs[0]);
        const long result = invoke_preserving_host_math_state(
            [x, svc_immediate] {
                return svc_immediate == kA32LibmLrintfSvcImmediate
                    ? std::lrint(x)
                    : std::lround(x);
            });
        regs[0] = static_cast<std::uint32_t>(static_cast<std::int64_t>(result));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmModfSvcImmediate: {
        const double x = read_double(regs, 0U);
        double integral{};
        const double fractional =
            invoke_preserving_host_math_state([&] { return std::modf(x, &integral); });
        if (!write_f64_le(memory, regs[2], integral)) {
            return A32HostServiceDisposition::Failed;
        }
        write_double(regs, fractional);
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmModffSvcImmediate: {
        const float x = read_float(regs[0]);
        float integral{};
        const float fractional =
            invoke_preserving_host_math_state([&] { return std::modf(x, &integral); });
        if (!write_f32_le(memory, regs[1], integral)) {
            return A32HostServiceDisposition::Failed;
        }
        write_float(regs, fractional);
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmNanfSvcImmediate: {
        std::string tag;
        if (!read_guest_c_string(
                memory, regs[0], options_.max_nan_tag_bytes, tag)) {
            return A32HostServiceDisposition::Failed;
        }
        write_float(regs, invoke_preserving_host_math_state(
            [&tag] { return std::nanf(tag.c_str()); }));
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmSincosSvcImmediate: {
        const double x = read_double(regs, 0U);
        const auto values = invoke_preserving_host_math_state(
            [x] { return std::array<double, 2>{std::sin(x), std::cos(x)}; });
        if (!write_f64_le(memory, regs[2], values[0]) ||
            !write_f64_le(memory, regs[3], values[1])) {
            return A32HostServiceDisposition::Failed;
        }
        return A32HostServiceDisposition::Handled;
    }
    case kA32LibmSincosfSvcImmediate: {
        const float x = read_float(regs[0]);
        const auto values = invoke_preserving_host_math_state(
            [x] { return std::array<float, 2>{std::sin(x), std::cos(x)}; });
        if (!write_f32_le(memory, regs[1], values[0]) ||
            !write_f32_le(memory, regs[2], values[1])) {
            return A32HostServiceDisposition::Failed;
        }
        return A32HostServiceDisposition::Handled;
    }
    default:
        return A32HostServiceDisposition::Failed;
    }
}

}  // namespace liba32android::compat
