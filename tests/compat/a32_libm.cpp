#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cerrno>
#include <cfenv>
#include <cmath>

#include "compat/a32_libm.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"

namespace {

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

std::int32_t read_i32(
    const LinearGuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return -9999;
    }
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return std::bit_cast<std::int32_t>(bits);
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

int test_exact_reference_values() {
    LinearGuestMemory memory{0x1000};
    A32LibmService service;

    if (!unary_double(service, memory, kA32LibmAcosSvcImmediate, 1.0, 0.0) ||
        !unary_double(service, memory, kA32LibmAsinSvcImmediate, 0.0, 0.0) ||
        !unary_double(service, memory, kA32LibmCosSvcImmediate, 0.0, 1.0) ||
        !unary_double(service, memory, kA32LibmExpSvcImmediate, 0.0, 1.0) ||
        !unary_double(service, memory, kA32LibmFloorSvcImmediate, 1.75, 1.0) ||
        !unary_double(service, memory, kA32LibmLogSvcImmediate, 1.0, 0.0) ||
        !unary_double(service, memory, kA32LibmLog10SvcImmediate, 1.0, 0.0) ||
        !unary_double(service, memory, kA32LibmSinSvcImmediate, 0.0, 0.0) ||
        !unary_double(service, memory, kA32LibmTanSvcImmediate, 0.0, 0.0)) {
        return fail("unary double libm exact-value case failed");
    }

    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_double(regs, 0U, 0.0);
    set_double(regs, 2U, 1.0);
    if (service.handle(memory, kA32LibmAtan2SvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 0.0) {
        return fail("atan2 softfp argument/result handling failed");
    }

    regs = {};
    set_double(regs, 0U, 2.0);
    set_double(regs, 2U, 3.0);
    if (service.handle(memory, kA32LibmPowSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 8.0) {
        return fail("pow softfp argument/result handling failed");
    }

    struct FloatCase {
        std::uint32_t svc;
        float input;
        float expected;
    };
    constexpr std::array<FloatCase, 3> float_cases{{
        {kA32LibmCosfSvcImmediate, 0.0f, 1.0f},
        {kA32LibmLog10fSvcImmediate, 1.0f, 0.0f},
        {kA32LibmSinfSvcImmediate, 0.0f, 0.0f},
    }};
    for (const auto& test_case : float_cases) {
        regs = {};
        set_float(regs, 0U, test_case.input);
        if (service.handle(memory, test_case.svc, regs, cpsr) !=
                A32HostServiceDisposition::Handled ||
            get_float(regs) != test_case.expected) {
            return fail("unary float libm exact-value case failed");
        }
    }

    regs = {};
    set_float(regs, 0U, 2.0f);
    set_float(regs, 1U, 3.0f);
    if (service.handle(memory, kA32LibmPowfSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_float(regs) != 8.0f) {
        return fail("powf softfp argument/result handling failed");
    }

    regs = {};
    set_double(regs, 0U, 8.0);
    regs[2] = 0x100U;
    if (service.handle(memory, kA32LibmFrexpSvcImmediate, regs, cpsr) !=
            A32HostServiceDisposition::Handled ||
        get_double(regs) != 0.5 ||
        read_i32(memory, 0x100U) != 4) {
        return fail("frexp result/exponent publication failed");
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
    if (service.handle(memory, 0xD2U, regs, cpsr) !=
        A32HostServiceDisposition::Unhandled) {
        return fail("unknown libm SVC was not unhandled");
    }
    return 0;
}


int test_host_math_state_preserved() {
    LinearGuestMemory memory{0x1000};
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

int test_frexp_bad_pointer_fails() {
    LinearGuestMemory memory{0x1000};
    A32LibmService service;
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    set_double(regs, 0U, 8.0);
    regs[2] = 0xffffffffU;
    if (service.handle(memory, kA32LibmFrexpSvcImmediate, regs, cpsr) !=
        A32HostServiceDisposition::Failed) {
        return fail("frexp invalid exponent pointer did not fail");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_reference_values(); status != 0) {
        return status;
    }
    if (const int status = test_host_math_state_preserved(); status != 0) {
        return status;
    }
    return test_frexp_bad_pointer_fails();
}
