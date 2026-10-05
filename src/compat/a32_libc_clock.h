#pragma once

#define LIBA32ANDROID_A32_CLOCK_GETTIME_SVC 0x132

#ifdef __cplusplus

#include <array>
#include <cstdint>

#include "compat/a32_libc_integer.h"
#include "compat/a32_pthread_sync.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32ClockGettimeSvcImmediate =
    LIBA32ANDROID_A32_CLOCK_GETTIME_SVC;

inline constexpr std::int32_t kA32ClockRealtime = 0;
inline constexpr std::int32_t kA32ClockMonotonic = 1;
inline constexpr std::int32_t kA32ClockMonotonicRaw = 4;

// Guest-visible LP32 clock_gettime compatibility over the existing
// embedding-owned deterministic clock seam. No host wall-clock API is called
// by this service.
class A32LibcClockService final
    : public runtime::A32HostServiceHandler {
public:
    A32LibcClockService(
        A32PthreadClock& clock,
        A32LibcErrnoSink& errno_sink) noexcept
        : clock_(clock), errno_sink_(errno_sink) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    A32PthreadClock& clock_;
    A32LibcErrnoSink& errno_sink_;
};

}  // namespace liba32android::compat

#endif
