#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

#include "compat/a32_libc_clock.h"
#include "memory/guest_memory.h"

namespace {

using namespace liba32android::compat;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
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

class FixtureClock final : public A32PthreadClock {
public:
    std::optional<std::int64_t> realtime{1'234'567'890LL};
    std::optional<std::int64_t> monotonic{2'000'000'003LL};
    std::optional<std::int64_t> monotonic_raw{3'999'999'999LL};

    [[nodiscard]] std::optional<std::int64_t> now_ns(
        A32PthreadClockId clock_id) const noexcept override {
        switch (clock_id) {
        case A32PthreadClockId::Realtime:
            return realtime;
        case A32PthreadClockId::Monotonic:
            return monotonic;
        case A32PthreadClockId::MonotonicRaw:
            return monotonic_raw;
        }
        return std::nullopt;
    }
};

struct Fixture {
    LinearGuestMemory memory{0x1000U};
    FixtureClock clock{};
    A32LibcGuestErrnoState errno_state{0x80U};
    A32LibcClockService service{clock, errno_state};

    A32HostServiceDisposition call(
        std::uint32_t clock_id,
        std::uint32_t timespec_address,
        std::array<std::uint32_t, 16>& regs) {
        regs = {};
        regs[0] = clock_id;
        regs[1] = timespec_address;
        std::uint32_t cpsr{};
        return service.handle(
            memory, kA32ClockGettimeSvcImmediate, regs, cpsr);
    }
};

int expect_timespec(
    const LinearGuestMemory& memory,
    std::uint32_t address,
    std::int32_t seconds,
    std::int32_t nanoseconds) {
    std::uint32_t sec{};
    std::uint32_t nsec{};
    if (!read_u32(memory, address, sec) ||
        !read_u32(memory, address + 4U, nsec) ||
        std::bit_cast<std::int32_t>(sec) != seconds ||
        std::bit_cast<std::int32_t>(nsec) != nanoseconds) {
        return fail("clock_gettime wrote the wrong LP32 timespec");
    }
    return 0;
}

int test_evidenced_clock_ids() {
    Fixture fixture;
    std::array<std::uint32_t, 16> regs{};

    if (fixture.call(0U, 0x100U, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        expect_timespec(fixture.memory, 0x100U, 1, 234'567'890) != 0) {
        return fail("CLOCK_REALTIME failed");
    }

    if (fixture.call(1U, 0x110U, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        expect_timespec(fixture.memory, 0x110U, 2, 3) != 0) {
        return fail("CLOCK_MONOTONIC failed");
    }

    if (fixture.call(4U, 0x120U, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        expect_timespec(fixture.memory, 0x120U, 3, 999'999'999) != 0) {
        return fail("CLOCK_MONOTONIC_RAW failed");
    }
    return 0;
}

int test_errors_do_not_publish_timespec() {
    Fixture fixture;
    constexpr std::array<std::uint8_t, 8> marker{{
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    }};
    if (!fixture.memory.write(0x180U, marker)) {
        return fail("could not stage timespec marker");
    }

    std::array<std::uint32_t, 16> regs{};
    if (fixture.call(2U, 0x180U, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1) {
        return fail("unsupported clock ID did not fail");
    }
    std::array<std::uint8_t, 8> observed{};
    std::uint32_t errno_word{};
    if (!fixture.memory.read(0x180U, observed) ||
        observed != marker ||
        !read_u32(fixture.memory, 0x80U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) != kA32AndroidEinval) {
        return fail("unsupported clock ID mutated output or missed EINVAL");
    }

    if (fixture.call(0U, 0U, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1 ||
        !read_u32(fixture.memory, 0x80U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) != kA32AndroidEfault) {
        return fail("null timespec pointer did not publish EFAULT");
    }

    fixture.clock.monotonic_raw.reset();
    if (fixture.call(4U, 0x180U, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1 ||
        !fixture.memory.read(0x180U, observed) ||
        observed != marker ||
        !read_u32(fixture.memory, 0x80U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) != kA32AndroidEinval) {
        return fail("unavailable logical clock did not fail deterministically");
    }

    fixture.clock.realtime =
        (static_cast<std::int64_t>(
             std::numeric_limits<std::int32_t>::max()) + 1LL) *
        1'000'000'000LL;
    if (fixture.call(0U, 0x180U, regs) !=
            A32HostServiceDisposition::Handled ||
        std::bit_cast<std::int32_t>(regs[0]) != -1 ||
        !fixture.memory.read(0x180U, observed) ||
        observed != marker ||
        !read_u32(fixture.memory, 0x80U, errno_word) ||
        std::bit_cast<std::int32_t>(errno_word) !=
            kA32AndroidEoverflow) {
        return fail("LP32 clock overflow did not publish EOVERFLOW");
    }
    return 0;
}

int test_negative_time_normalization_and_unknown_svc() {
    Fixture fixture;
    fixture.clock.realtime = -1LL;
    std::array<std::uint32_t, 16> regs{};
    if (fixture.call(0U, 0x140U, regs) !=
            A32HostServiceDisposition::Handled ||
        regs[0] != 0U ||
        expect_timespec(
            fixture.memory, 0x140U, -1, 999'999'999) != 0) {
        return fail("negative logical time was not normalized");
    }

    std::uint32_t cpsr{};
    if (fixture.service.handle(
            fixture.memory, 0x133U, regs, cpsr) !=
            A32HostServiceDisposition::Unhandled) {
        return fail("unknown clock service ID was not unhandled");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_evidenced_clock_ids(); status != 0) {
        return status;
    }
    if (const int status = test_errors_do_not_publish_timespec(); status != 0) {
        return status;
    }
    return test_negative_time_normalization_and_unknown_svc();
}
