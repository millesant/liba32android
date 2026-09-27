#pragma once

#define LIBA32ANDROID_A32_LIBC_ATOI_SVC 0xAB
#define LIBA32ANDROID_A32_LIBC_STRTOL_SVC 0xAC

#ifdef __cplusplus

#include <array>
#include <cstdint>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32LibcAtoiSvcImmediate =
    LIBA32ANDROID_A32_LIBC_ATOI_SVC;
inline constexpr std::uint32_t kA32LibcStrtolSvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRTOL_SVC;

// Android/Linux guest errno numbers used by the integer-conversion contract.
inline constexpr std::int32_t kA32AndroidEinval = 22;
inline constexpr std::int32_t kA32AndroidErange = 34;

class A32LibcErrnoSink {
public:
    virtual ~A32LibcErrnoSink() = default;
    virtual void set_errno(std::int32_t value) noexcept = 0;
};

struct A32LibcIntegerOptions {
    // Maximum number of guest bytes that one conversion may examine.
    std::uint32_t max_parse_bytes{};
};

class A32LibcIntegerService final
    : public runtime::A32HostServiceHandler {
public:
    A32LibcIntegerService(
        A32LibcErrnoSink& errno_sink,
        A32LibcIntegerOptions options) noexcept
        : errno_sink_(errno_sink),
          options_(options) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] A32LibcIntegerOptions options() const noexcept {
        return options_;
    }

private:
    A32LibcErrnoSink& errno_sink_;
    A32LibcIntegerOptions options_;
};

}  // namespace liba32android::compat

#endif
