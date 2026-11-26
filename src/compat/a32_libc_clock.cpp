#include "compat/a32_libc_clock.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>

#include "compat/a32_android_errno.h"
#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;

[[nodiscard]] std::optional<A32PthreadClockId> decode_clock_id(
    std::int32_t clock_id) noexcept {
    switch (clock_id) {
    case kA32ClockRealtime:
        return A32PthreadClockId::Realtime;
    case kA32ClockMonotonic:
        return A32PthreadClockId::Monotonic;
    case kA32ClockMonotonicRaw:
        return A32PthreadClockId::MonotonicRaw;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::array<std::uint8_t, 8> encode_timespec(
    std::int32_t seconds,
    std::int32_t nanoseconds) noexcept {
    const std::uint32_t sec = std::bit_cast<std::uint32_t>(seconds);
    const std::uint32_t nsec = std::bit_cast<std::uint32_t>(nanoseconds);
    return {{
        static_cast<std::uint8_t>(sec),
        static_cast<std::uint8_t>(sec >> 8U),
        static_cast<std::uint8_t>(sec >> 16U),
        static_cast<std::uint8_t>(sec >> 24U),
        static_cast<std::uint8_t>(nsec),
        static_cast<std::uint8_t>(nsec >> 8U),
        static_cast<std::uint8_t>(nsec >> 16U),
        static_cast<std::uint8_t>(nsec >> 24U),
    }};
}

}  // namespace

runtime::A32HostServiceDisposition A32LibcClockService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != kA32ClockGettimeSvcImmediate) {
        return A32HostServiceDisposition::Unhandled;
    }

    const auto libc_error = [&](std::int32_t error) {
        if (!errno_sink_.set_errno(memory, error)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = std::numeric_limits<std::uint32_t>::max();
        return A32HostServiceDisposition::Handled;
    };

    const auto clock_id =
        decode_clock_id(std::bit_cast<std::int32_t>(regs[0]));
    if (!clock_id.has_value()) {
        return libc_error(kA32AndroidEinval);
    }

    const std::uint32_t timespec_address = regs[1];
    if (timespec_address == 0U ||
        timespec_address >
            std::numeric_limits<std::uint32_t>::max() - 7U) {
        return libc_error(kA32AndroidEfault);
    }

    const auto now_ns = clock_.now_ns(*clock_id);
    if (!now_ns.has_value()) {
        return libc_error(kA32AndroidEinval);
    }

    std::int64_t seconds = *now_ns / kNanosecondsPerSecond;
    std::int64_t nanoseconds = *now_ns % kNanosecondsPerSecond;
    if (nanoseconds < 0) {
        nanoseconds += kNanosecondsPerSecond;
        --seconds;
    }

    if (seconds < std::numeric_limits<std::int32_t>::min() ||
        seconds > std::numeric_limits<std::int32_t>::max()) {
        return libc_error(kA32AndroidEoverflow);
    }

    const auto encoded = encode_timespec(
        static_cast<std::int32_t>(seconds),
        static_cast<std::int32_t>(nanoseconds));
    if (!memory.write(timespec_address, encoded)) {
        return libc_error(kA32AndroidEfault);
    }

    regs[0] = 0U;
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
