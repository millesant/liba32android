#pragma once

#define LIBA32ANDROID_A32_AEABI_ATEXIT_SVC 0xD2

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32AeabiAtexitSvcImmediate =
    LIBA32ANDROID_A32_AEABI_ATEXIT_SVC;

struct A32AeabiAtexitRecord {
    std::uint32_t object{};
    std::uint32_t destructor{};
    std::uint32_t dso_handle{};
};

// Bounded caller-owned registration state for ARM EABI __aeabi_atexit.
// Guest object/destructor/DSO values remain opaque logical 32-bit values.
// Capacity exhaustion is an ordinary guest-visible registration failure
// (r0 == -1), not a host-service dispatch failure.
class A32AeabiAtexitService final
    : public runtime::A32HostServiceHandler {
public:
    explicit A32AeabiAtexitService(
        std::span<A32AeabiAtexitRecord> records) noexcept
        : records_(records) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] std::size_t record_count() const noexcept {
        return record_count_;
    }

    [[nodiscard]] std::span<const A32AeabiAtexitRecord> records() const noexcept {
        return std::span<const A32AeabiAtexitRecord>{
            records_.data(), record_count_};
    }

private:
    std::span<A32AeabiAtexitRecord> records_;
    std::size_t record_count_{};
};

}  // namespace liba32android::compat

#endif
