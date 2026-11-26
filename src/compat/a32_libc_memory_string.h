#pragma once

#define LIBA32ANDROID_A32_LIBC_MEMCPY_SVC 0xA1
#define LIBA32ANDROID_A32_LIBC_MEMSET_SVC 0xA2
#define LIBA32ANDROID_A32_LIBC_MEMCMP_SVC 0xA3
#define LIBA32ANDROID_A32_LIBC_MEMCHR_SVC 0xA4
#define LIBA32ANDROID_A32_LIBC_STRLEN_SVC 0xA5
#define LIBA32ANDROID_A32_LIBC_STRCMP_SVC 0xA6
#define LIBA32ANDROID_A32_LIBC_STRNCMP_SVC 0xA7
#define LIBA32ANDROID_A32_LIBC_MEMMEM_SVC 0xA8
#define LIBA32ANDROID_A32_LIBC_STRCPY_SVC 0xA9
#define LIBA32ANDROID_A32_LIBC_STRNCPY_SVC 0xAA
#define LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC 0xB2

#ifdef __cplusplus

#include <array>
#include <cstdint>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32LibcMemcpySvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMCPY_SVC;
inline constexpr std::uint32_t kA32LibcMemsetSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMSET_SVC;
inline constexpr std::uint32_t kA32LibcMemcmpSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMCMP_SVC;
inline constexpr std::uint32_t kA32LibcMemchrSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMCHR_SVC;
inline constexpr std::uint32_t kA32LibcStrlenSvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRLEN_SVC;
inline constexpr std::uint32_t kA32LibcStrcmpSvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRCMP_SVC;
inline constexpr std::uint32_t kA32LibcStrncmpSvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRNCMP_SVC;
inline constexpr std::uint32_t kA32LibcMemmemSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMMEM_SVC;
inline constexpr std::uint32_t kA32LibcStrcpySvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRCPY_SVC;
inline constexpr std::uint32_t kA32LibcStrncpySvcImmediate =
    LIBA32ANDROID_A32_LIBC_STRNCPY_SVC;
inline constexpr std::uint32_t kA32LibcMemmoveSvcImmediate =
    LIBA32ANDROID_A32_LIBC_MEMMOVE_SVC;

struct A32LibcMemoryStringOptions {
    std::uint32_t max_transfer_bytes{};
    std::uint32_t max_string_bytes{};
};

class A32LibcMemoryStringService final
    : public runtime::A32HostServiceHandler {
public:
    explicit A32LibcMemoryStringService(
        A32LibcMemoryStringOptions options) noexcept
        : options_(options) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] A32LibcMemoryStringOptions options() const noexcept {
        return options_;
    }

private:
    A32LibcMemoryStringOptions options_;
};

}  // namespace liba32android::compat

#endif
