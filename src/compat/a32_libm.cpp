#include "compat/a32_libm.h"

#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

static_assert(sizeof(float) == 4U);
static_assert(sizeof(double) == 8U);

[[nodiscard]] bool is_libm_svc(std::uint32_t svc) noexcept {
    return svc >= kA32LibmAcosSvcImmediate &&
           svc <= kA32LibmTanSvcImmediate;
}

[[nodiscard]] double read_double(
    const std::array<std::uint32_t, 16>& regs,
    std::size_t first) noexcept {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(regs[first]) |
        (static_cast<std::uint64_t>(regs[first + 1U]) << 32U);
    return std::bit_cast<double>(bits);
}

void write_double(
    std::array<std::uint32_t, 16>& regs,
    double value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    regs[0] = static_cast<std::uint32_t>(bits);
    regs[1] = static_cast<std::uint32_t>(bits >> 32U);
}

[[nodiscard]] float read_float(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

void write_float(
    std::array<std::uint32_t, 16>& regs,
    float value) noexcept {
    regs[0] = std::bit_cast<std::uint32_t>(value);
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

}  // namespace

runtime::A32HostServiceDisposition A32LibmService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_libm_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }

    if (svc_immediate == kA32LibmCosfSvcImmediate ||
        svc_immediate == kA32LibmLog10fSvcImmediate ||
        svc_immediate == kA32LibmSinfSvcImmediate) {
        const float x = read_float(regs[0]);
        float result{};
        if (svc_immediate == kA32LibmCosfSvcImmediate) {
            result = invoke_preserving_host_math_state(
                [x] { return std::cos(x); });
        } else if (svc_immediate == kA32LibmLog10fSvcImmediate) {
            result = invoke_preserving_host_math_state(
                [x] { return std::log10(x); });
        } else {
            result = invoke_preserving_host_math_state(
                [x] { return std::sin(x); });
        }
        write_float(regs, result);
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibmPowfSvcImmediate) {
        const float base = read_float(regs[0]);
        const float exponent = read_float(regs[1]);
        write_float(
            regs,
            invoke_preserving_host_math_state(
                [base, exponent] { return std::pow(base, exponent); }));
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibmAtan2SvcImmediate) {
        const double y = read_double(regs, 0U);
        const double x = read_double(regs, 2U);
        write_double(
            regs,
            invoke_preserving_host_math_state(
                [y, x] { return std::atan2(y, x); }));
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibmPowSvcImmediate) {
        const double base = read_double(regs, 0U);
        const double exponent = read_double(regs, 2U);
        write_double(
            regs,
            invoke_preserving_host_math_state(
                [base, exponent] { return std::pow(base, exponent); }));
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32LibmFrexpSvcImmediate) {
        const double x = read_double(regs, 0U);
        std::int32_t exponent{};
        const double result = invoke_preserving_host_math_state(
            [&] {
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

    if (svc_immediate == kA32LibmLdexpSvcImmediate) {
        const double x = read_double(regs, 0U);
        const std::int32_t exponent =
            std::bit_cast<std::int32_t>(regs[2]);
        write_double(
            regs,
            invoke_preserving_host_math_state(
                [x, exponent] {
                    return std::ldexp(x, static_cast<int>(exponent));
                }));
        return A32HostServiceDisposition::Handled;
    }

    const double x = read_double(regs, 0U);
    double result{};
    switch (svc_immediate) {
    case kA32LibmAcosSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::acos(x); });
        break;
    case kA32LibmAsinSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::asin(x); });
        break;
    case kA32LibmCosSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::cos(x); });
        break;
    case kA32LibmExpSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::exp(x); });
        break;
    case kA32LibmFloorSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::floor(x); });
        break;
    case kA32LibmLogSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::log(x); });
        break;
    case kA32LibmLog10SvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::log10(x); });
        break;
    case kA32LibmSinSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::sin(x); });
        break;
    case kA32LibmTanSvcImmediate:
        result = invoke_preserving_host_math_state([x] { return std::tan(x); });
        break;
    default:
        return A32HostServiceDisposition::Failed;
    }
    write_double(regs, result);
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
