#pragma once

#define LIBA32ANDROID_A32_LIBC_ATOI_SVC 0xAB
#define LIBA32ANDROID_A32_LIBC_STRTOL_SVC 0xAC
#define LIBA32ANDROID_A32_LIBC_ERRNO_SVC 0xAD

#ifdef __cplusplus

#include <array>
#include <cstdint>

#include "compat/a32_android_errno.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32LibcAtoiSvcImmediate =
    LIBA32ANDROID_A32_LIBC_ATOI_SVC;
inline constexpr std::uint32_t kA32LibcStrtolSvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRTOL_SVC;
inline constexpr std::uint32_t kA32LibcErrnoSvcImmediate =
    LIBA32ANDROID_A32_LIBC_ERRNO_SVC;

class A32LibcErrnoSink {
public:
    virtual ~A32LibcErrnoSink() = default;
    [[nodiscard]] virtual bool set_errno(
        memory::GuestMemory& memory,
        std::int32_t value) noexcept = 0;
};

class A32LibcGuestErrnoState final
    : public A32LibcErrnoSink,
      public runtime::A32HostServiceHandler {
public:
    explicit A32LibcGuestErrnoState(
        std::uint32_t errno_address) noexcept
        : errno_address_(errno_address) {}

    [[nodiscard]] bool set_errno(
        memory::GuestMemory& memory,
        std::int32_t value) noexcept override;

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] std::uint32_t errno_address() const noexcept {
        return errno_address_;
    }

private:
    std::uint32_t errno_address_;
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
