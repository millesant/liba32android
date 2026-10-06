#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "compat/a32_libm.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

using liba32android::compat::A32LibmOptions;
using liba32android::compat::A32LibmService;
using liba32android::compat::kA32LibmAcosSvcImmediate;
using liba32android::compat::kA32LibmAsinSvcImmediate;
using liba32android::compat::kA32LibmAtan2SvcImmediate;
using liba32android::compat::kA32LibmCosSvcImmediate;
using liba32android::compat::kA32LibmCosfSvcImmediate;
using liba32android::compat::kA32LibmExpSvcImmediate;
using liba32android::compat::kA32LibmFloorSvcImmediate;
using liba32android::compat::kA32LibmFrexpSvcImmediate;
using liba32android::compat::kA32LibmLdexpSvcImmediate;
using liba32android::compat::kA32LibmLogSvcImmediate;
using liba32android::compat::kA32LibmLog10SvcImmediate;
using liba32android::compat::kA32LibmLog10fSvcImmediate;
using liba32android::compat::kA32LibmPowSvcImmediate;
using liba32android::compat::kA32LibmPowfSvcImmediate;
using liba32android::compat::kA32LibmSinSvcImmediate;
using liba32android::compat::kA32LibmSinfSvcImmediate;
using liba32android::compat::kA32LibmTanSvcImmediate;
using liba32android::compat::kA32LibmAcosfSvcImmediate;
using liba32android::compat::kA32LibmAtanSvcImmediate;
using liba32android::compat::kA32LibmAtan2fSvcImmediate;
using liba32android::compat::kA32LibmAtanfSvcImmediate;
using liba32android::compat::kA32LibmCbrtSvcImmediate;
using liba32android::compat::kA32LibmCbrtfSvcImmediate;
using liba32android::compat::kA32LibmCeilSvcImmediate;
using liba32android::compat::kA32LibmCeilfSvcImmediate;
using liba32android::compat::kA32LibmCoshSvcImmediate;
using liba32android::compat::kA32LibmExp2SvcImmediate;
using liba32android::compat::kA32LibmExp2fSvcImmediate;
using liba32android::compat::kA32LibmExpfSvcImmediate;
using liba32android::compat::kA32LibmExpm1SvcImmediate;
using liba32android::compat::kA32LibmFabsSvcImmediate;
using liba32android::compat::kA32LibmFloorfSvcImmediate;
using liba32android::compat::kA32LibmFmaxSvcImmediate;
using liba32android::compat::kA32LibmFmaxfSvcImmediate;
using liba32android::compat::kA32LibmFminfSvcImmediate;
using liba32android::compat::kA32LibmFmodSvcImmediate;
using liba32android::compat::kA32LibmFmodfSvcImmediate;
using liba32android::compat::kA32LibmFrexpfSvcImmediate;
using liba32android::compat::kA32LibmHypotSvcImmediate;
using liba32android::compat::kA32LibmHypotfSvcImmediate;
using liba32android::compat::kA32LibmLdexpfSvcImmediate;
using liba32android::compat::kA32LibmLlrintSvcImmediate;
using liba32android::compat::kA32LibmLlrintfSvcImmediate;
using liba32android::compat::kA32LibmLlroundSvcImmediate;
using liba32android::compat::kA32LibmLlroundfSvcImmediate;
using liba32android::compat::kA32LibmLog1pSvcImmediate;
using liba32android::compat::kA32LibmLogfSvcImmediate;
using liba32android::compat::kA32LibmLrintSvcImmediate;
using liba32android::compat::kA32LibmLrintfSvcImmediate;
using liba32android::compat::kA32LibmLroundSvcImmediate;
using liba32android::compat::kA32LibmLroundfSvcImmediate;
using liba32android::compat::kA32LibmModfSvcImmediate;
using liba32android::compat::kA32LibmModffSvcImmediate;
using liba32android::compat::kA32LibmNanfSvcImmediate;
using liba32android::compat::kA32LibmRintSvcImmediate;
using liba32android::compat::kA32LibmRintfSvcImmediate;
using liba32android::compat::kA32LibmRoundSvcImmediate;
using liba32android::compat::kA32LibmRoundfSvcImmediate;
using liba32android::compat::kA32LibmScalbnSvcImmediate;
using liba32android::compat::kA32LibmSincosSvcImmediate;
using liba32android::compat::kA32LibmSincosfSvcImmediate;
using liba32android::compat::kA32LibmSinhSvcImmediate;
using liba32android::compat::kA32LibmTanfSvcImmediate;
using liba32android::compat::kA32LibmTanhSvcImmediate;
using liba32android::compat::kA32LibmTruncSvcImmediate;
using liba32android::compat::kA32LibmTruncfSvcImmediate;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

void set_double(
    std::array<std::uint32_t, 16>& regs,
    std::size_t first,
    double value) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    regs[first] = static_cast<std::uint32_t>(bits);
    regs[first + 1U] = static_cast<std::uint32_t>(bits >> 32U);
}

double get_double(const std::array<std::uint32_t, 16>& regs) {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(regs[0]) |
        (static_cast<std::uint64_t>(regs[1]) << 32U);
    return std::bit_cast<double>(bits);
}

void set_float(
    std::array<std::uint32_t, 16>& regs,
    std::size_t index,
    float value) {
    regs[index] = std::bit_cast<std::uint32_t>(value);
}

float get_float(const std::array<std::uint32_t, 16>& regs) {
    return std::bit_cast<float>(regs[0]);
}

std::int64_t get_i64(const std::array<std::uint32_t, 16>& regs) {
    const std::uint64_t bits =
        static_cast<std::uint64_t>(regs[0]) |
        (static_cast<std::uint64_t>(regs[1]) << 32U);
    return std::bit_cast<std::int64_t>(bits);
}

bool read_u32(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

bool read_u64(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::uint64_t& value) {
    std::array<std::uint8_t, 8> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = 0U;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return true;
}

bool read_i32(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::int32_t& value) {
    std::uint32_t bits{};
    if (!read_u32(memory, address, bits)) return false;
    value = std::bit_cast<std::int32_t>(bits);
    return true;
}

bool read_float_value(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    float& value) {
    std::uint32_t bits{};
    if (!read_u32(memory, address, bits)) return false;
    value = std::bit_cast<float>(bits);
    return true;
}

bool read_double_value(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    double& value) {
    std::uint64_t bits{};
    if (!read_u64(memory, address, bits)) return false;
    value = std::bit_cast<double>(bits);
    return true;
}

bool unary_double(
    A32LibmService& service,
    LinearGuestMemory& memory,
    std::uint32_t svc,
    double input,
    double expected) {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_double(regs, 0U, input);
    return service.handle(memory, svc, regs, cpsr) ==
               A32HostServiceDisposition::Handled &&
           get_double(regs) == expected;
}

bool unary_float(
    A32LibmService& service,
    LinearGuestMemory& memory,
    std::uint32_t svc,
    float input,
    float expected) {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_float(regs, 0U, input);
    return service.handle(memory, svc, regs, cpsr) ==
               A32HostServiceDisposition::Handled &&
           get_float(regs) == expected;
}

bool binary_double(
    A32LibmService& service,
    LinearGuestMemory& memory,
    std::uint32_t svc,
    double first,
    double second,
    double expected) {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_double(regs, 0U, first);
    set_double(regs, 2U, second);
    return service.handle(memory, svc, regs, cpsr) ==
               A32HostServiceDisposition::Handled &&
           get_double(regs) == expected;
}

bool binary_float(
    A32LibmService& service,
    LinearGuestMemory& memory,
    std::uint32_t svc,
    float first,
    float second,
    float expected) {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_float(regs, 0U, first);
    set_float(regs, 1U, second);
    return service.handle(memory, svc, regs, cpsr) ==
               A32HostServiceDisposition::Handled &&
           get_float(regs) == expected;
}

int test_exact_reference_values() {
    LinearGuestMemory memory{0x2000U};
    A32LibmService service;

    struct UnaryDoubleCase {
        std::uint32_t svc;
        double input;
        double expected;
    };
    constexpr std::array<UnaryDoubleCase, 22> unary_doubles{{
        {kA32LibmAcosSvcImmediate, 1, 0},
        {kA32LibmAsinSvcImmediate, 0, 0},
        {kA32LibmAtanSvcImmediate, 0, 0},
        {kA32LibmCbrtSvcImmediate, 8, 2},
        {kA32LibmCeilSvcImmediate, 1.25, 2},
        {kA32LibmCosSvcImmediate, 0, 1},
        {kA32LibmCoshSvcImmediate, 0, 1},
        {kA32LibmExpSvcImmediate, 0, 1},
        {kA32LibmExp2SvcImmediate, 3, 8},
        {kA32LibmExpm1SvcImmediate, 0, 0},
        {kA32LibmFabsSvcImmediate, -2, 2},
        {kA32LibmFloorSvcImmediate, 1.75, 1},
        {kA32LibmLogSvcImmediate, 1, 0},
        {kA32LibmLog10SvcImmediate, 1, 0},
        {kA32LibmLog1pSvcImmediate, 0, 0},
        {kA32LibmRintSvcImmediate, 2, 2},
        {kA32LibmRoundSvcImmediate, 1.5, 2},
        {kA32LibmSinSvcImmediate, 0, 0},
        {kA32LibmSinhSvcImmediate, 0, 0},
        {kA32LibmTanSvcImmediate, 0, 0},
        {kA32LibmTanhSvcImmediate, 0, 0},
        {kA32LibmTruncSvcImmediate, 1.75, 1},
    }};
    for (const auto& test_case : unary_doubles) {
        if (!unary_double(
                service, memory, test_case.svc,
                test_case.input, test_case.expected)) {
            return fail("unary double libm exact-value case failed");
        }
    }

    struct UnaryFloatCase {
        std::uint32_t svc;
        float input;
        float expected;
    };
    constexpr std::array<UnaryFloatCase, 15> unary_floats{{
        {kA32LibmAcosfSvcImmediate, 1f, 0f},
        {kA32LibmAtanfSvcImmediate, 0f, 0f},
        {kA32LibmCbrtfSvcImmediate, 8f, 2f},
        {kA32LibmCeilfSvcImmediate, 1.25f, 2f},
        {kA32LibmCosfSvcImmediate, 0f, 1f},
        {kA32LibmExp2fSvcImmediate, 3f, 8f},
        {kA32LibmExpfSvcImmediate, 0f, 1f},
        {kA32LibmFloorfSvcImmediate, 1.75f, 1f},
        {kA32LibmLog10fSvcImmediate, 1f, 0f},
        {kA32LibmLogfSvcImmediate, 1f, 0f},
        {kA32LibmRintfSvcImmediate, 2f, 2f},
        {kA32LibmRoundfSvcImmediate, 1.5f, 2f},
        {kA32LibmSinfSvcImmediate, 0f, 0f},
        {kA32LibmTanfSvcImmediate, 0f, 0f},
        {kA32LibmTruncfSvcImmediate, 1.75f, 1f},
    }};
    for (const auto& test_case : unary_floats) {
        if (!unary_float(
                service, memory, test_case.svc,
                test_case.input, test_case.expected)) {
            return fail("unary float libm exact-value case failed");
        }
    }

    struct BinaryDoubleCase {
        std::uint32_t svc;
        double first;
        double second;
        double expected;
    };
    constexpr std::array<BinaryDoubleCase, 5> binary_doubles{{
        {kA32LibmAtan2SvcImmediate, 0, 1, 0},
        {kA32LibmFmaxSvcImmediate, 2, 3, 3},
        {kA32LibmFmodSvcImmediate, 5, 2, 1},
        {kA32LibmHypotSvcImmediate, 3, 4, 5},
        {kA32LibmPowSvcImmediate, 2, 3, 8},
    }};
    for (const auto& test_case : binary_doubles) {
        if (!binary_double(
                service, memory, test_case.svc,
                test_case.first, test_case.second, test_case.expected)) {
            return fail("binary double libm exact-value case failed");
        }
    }

    struct BinaryFloatCase {
        std::uint32_t svc;
        float first;
        float second;
        float expected;
    };
    constexpr std::array<BinaryFloatCase, 6> binary_floats{{
        {kA32LibmAtan2fSvcImmediate, 0f, 1f, 0f},
        {kA32LibmFmaxfSvcImmediate, 2f, 3f, 3f},
        {kA32LibmFminfSvcImmediate, 2f, 3f, 2f},
        {kA32LibmFmodfSvcImmediate, 5f, 2f, 1f},
        {kA32LibmHypotfSvcImmediate, 3f, 4f, 5f},
        {kA32LibmPowfSvcImmediate, 2f, 3f, 8f},
    }};
    for (const auto& test_case : binary_floats) {
        if (!binary_float(
                service, memory, test_case.svc,
                test_case.first, test_case.second, test_case.expected)) {
            return fail("binary float libm exact-value case failed");
        }
    }
    return 0;
}

int test_pointer_and_integer_abi_shapes() {
    LinearGuestMemory memory{0x2000U};
    A32LibmService service;
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    std::int32_t exponent{};

    set_double(regs, 0U, 8.0);
    regs[2] = 0x100U;
    if (service.handle(memory, kA32LibmFrexpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 0.5 ||
        !read_i32(memory, 0x100U, exponent) || exponent != 4) {
        return fail("frexp result/exponent publication failed");
    }

    regs = {};
    set_float(regs, 0U, 8.0f);
    regs[1] = 0x110U;
    if (service.handle(memory, kA32LibmFrexpfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_float(regs) != 0.5f ||
        !read_i32(memory, 0x110U, exponent) || exponent != 4) {
        return fail("frexpf result/exponent publication failed");
    }

    regs = {};
    set_double(regs, 0U, 0.5);
    regs[2] = std::bit_cast<std::uint32_t>(std::int32_t{4});
    if (service.handle(memory, kA32LibmLdexpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 8.0) {
        return fail("ldexp softfp integer argument handling failed");
    }

    regs = {};
    set_float(regs, 0U, 0.5f);
    regs[1] = std::bit_cast<std::uint32_t>(std::int32_t{4});
    if (service.handle(memory, kA32LibmLdexpfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_float(regs) != 8.0f) {
        return fail("ldexpf softfp integer argument handling failed");
    }

    regs = {};
    set_double(regs, 0U, 0.5);
    regs[2] = std::bit_cast<std::uint32_t>(std::int32_t{4});
    if (service.handle(memory, kA32LibmScalbnSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 8.0) {
        return fail("scalbn softfp integer argument handling failed");
    }

    for (const auto svc : {
             kA32LibmLlrintSvcImmediate,
             kA32LibmLlroundSvcImmediate}) {
        regs = {};
        set_double(regs, 0U, 2.0);
        if (service.handle(memory, svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            get_i64(regs) != 2) {
            return fail("double-to-int64 libm result handling failed");
        }
    }
    for (const auto svc : {
             kA32LibmLlrintfSvcImmediate,
             kA32LibmLlroundfSvcImmediate}) {
        regs = {};
        set_float(regs, 0U, 2.0f);
        if (service.handle(memory, svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            get_i64(regs) != 2) {
            return fail("float-to-int64 libm result handling failed");
        }
    }
    for (const auto svc : {
             kA32LibmLrintSvcImmediate,
             kA32LibmLroundSvcImmediate}) {
        regs = {};
        set_double(regs, 0U, 2.0);
        if (service.handle(memory, svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            std::bit_cast<std::int32_t>(regs[0]) != 2) {
            return fail("double-to-long ARM32 result handling failed");
        }
    }
    for (const auto svc : {
             kA32LibmLrintfSvcImmediate,
             kA32LibmLroundfSvcImmediate}) {
        regs = {};
        set_float(regs, 0U, 2.0f);
        if (service.handle(memory, svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            std::bit_cast<std::int32_t>(regs[0]) != 2) {
            return fail("float-to-long ARM32 result handling failed");
        }
    }

    double double_integral{};
    regs = {};
    set_double(regs, 0U, 1.5);
    regs[2] = 0x120U;
    if (service.handle(memory, kA32LibmModfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 0.5 ||
        !read_double_value(memory, 0x120U, double_integral) ||
        double_integral != 1.0) {
        return fail("modf softfp pointer/result handling failed");
    }

    float float_integral{};
    regs = {};
    set_float(regs, 0U, 1.5f);
    regs[1] = 0x130U;
    if (service.handle(memory, kA32LibmModffSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_float(regs) != 0.5f ||
        !read_float_value(memory, 0x130U, float_integral) ||
        float_integral != 1.0f) {
        return fail("modff softfp pointer/result handling failed");
    }

    double sine{};
    double cosine{};
    regs = {};
    set_double(regs, 0U, 0.0);
    regs[2] = 0x140U;
    regs[3] = 0x148U;
    if (service.handle(memory, kA32LibmSincosSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        !read_double_value(memory, 0x140U, sine) ||
        !read_double_value(memory, 0x148U, cosine) ||
        sine != 0.0 || cosine != 1.0) {
        return fail("sincos pointer-result handling failed");
    }

    float sinef{};
    float cosinef{};
    regs = {};
    set_float(regs, 0U, 0.0f);
    regs[1] = 0x160U;
    regs[2] = 0x164U;
    if (service.handle(memory, kA32LibmSincosfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        !read_float_value(memory, 0x160U, sinef) ||
        !read_float_value(memory, 0x164U, cosinef) ||
        sinef != 0.0f || cosinef != 1.0f) {
        return fail("sincosf pointer-result handling failed");
    }

    constexpr std::array<std::uint8_t, 1> empty_tag{{0U}};
    if (!memory.write(0x180U, empty_tag)) {
        return fail("could not stage nanf payload tag");
    }
    regs = {};
    regs[0] = 0x180U;
    if (service.handle(memory, kA32LibmNanfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        !std::isnan(get_float(regs))) {
        return fail("nanf bounded guest-string handling failed");
    }
    return 0;
}

int test_host_math_state_preserved() {
    LinearGuestMemory memory{0x1000U};
    A32LibmService service;

    const int original_errno = errno;
    std::fenv_t original_environment{};
    if (std::fegetenv(&original_environment) != 0) {
        return fail("could not snapshot host floating-point environment");
    }
    bool setup_ok = std::fesetround(FE_DOWNWARD) == 0 &&
                    std::feclearexcept(FE_ALL_EXCEPT) == 0;
    if (setup_ok && (math_errhandling & MATH_ERREXCEPT) != 0) {
        setup_ok = std::feraiseexcept(FE_DIVBYZERO) == 0;
    }
    errno = 123;

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_double(regs, 0U, -1.0);
    const auto disposition =
        service.handle(memory, kA32LibmLogSvcImmediate, regs, cpsr);

    const bool errno_preserved = errno == 123;
    const bool rounding_preserved = std::fegetround() == FE_DOWNWARD;
    bool exceptions_preserved = true;
    if ((math_errhandling & MATH_ERREXCEPT) != 0) {
        const int raised = std::fetestexcept(FE_ALL_EXCEPT);
        exceptions_preserved =
            (raised & FE_DIVBYZERO) != 0 &&
            (raised & FE_INVALID) == 0;
    }

    static_cast<void>(std::fesetenv(&original_environment));
    errno = original_errno;
    if (!setup_ok ||
        disposition != A32HostServiceDisposition::Handled ||
        !errno_preserved ||
        !rounding_preserved ||
        !exceptions_preserved) {
        return fail("libm service leaked host errno/fenv state");
    }
    return 0;
}

int test_bounded_failures_and_unknown_svc() {
    LinearGuestMemory memory{0x1000U};
    A32LibmService service{A32LibmOptions{.max_nan_tag_bytes = 3U}};
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};

    set_double(regs, 0U, 8.0);
    regs[2] = 0xffffffffU;
    if (service.handle(memory, kA32LibmFrexpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Failed) {
        return fail("frexp invalid exponent pointer did not fail");
    }

    constexpr std::array<std::uint8_t, 5> long_tag{{'1','2','3','4',0U}};
    if (!memory.write(0x100U, long_tag)) {
        return fail("could not stage over-limit nanf tag");
    }
    regs = {};
    regs[0] = 0x100U;
    if (service.handle(memory, kA32LibmNanfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Failed) {
        return fail("nanf over-limit guest tag did not fail");
    }

    regs = {};
    set_double(regs, 0U, 1.5);
    regs[2] = 0xffffffffU;
    if (service.handle(memory, kA32LibmModfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Failed) {
        return fail("modf invalid integral pointer did not fail");
    }

    regs = {};
    if (service.handle(memory, 0x1B1U, regs, cpsr) !=
            A32HostServiceDisposition::Unhandled) {
        return fail("unknown libm SVC was not unhandled");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_reference_values(); status != 0) {
        return status;
    }
    if (const int status = test_pointer_and_integer_abi_shapes(); status != 0) {
        return status;
    }
    if (const int status = test_host_math_state_preserved(); status != 0) {
        return status;
    }
    return test_bounded_failures_and_unknown_svc();
}
