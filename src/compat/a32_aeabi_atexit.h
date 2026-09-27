#pragma once

#define LIBA32ANDROID_A32_AEABI_ATEXIT_SVC 0xD2

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cpu/a32_cpu.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32AeabiAtexitSvcImmediate =
    LIBA32ANDROID_A32_AEABI_ATEXIT_SVC;

enum class A32AeabiAtexitRecordStatus : std::uint8_t {
    Pending = 0,
    Complete,
    Failed,
};

struct A32AeabiAtexitRecord {
    std::uint32_t object{};
    std::uint32_t destructor{};
    std::uint32_t dso_handle{};
    A32AeabiAtexitRecordStatus status{
        A32AeabiAtexitRecordStatus::Pending};
};

struct A32AeabiFinalizeOptions {
    std::uint32_t stack_top{};
    std::uint32_t return_pc{};
    std::size_t max_instructions_per_call{};
    std::uint32_t max_callbacks{};
};

enum class A32AeabiFinalizeError : std::uint8_t {
    None = 0,
    InvalidOptions,
    CallbackLimitExceeded,
    InvalidRecordState,
    InvalidFunctionAddress,
    CpuException,
    MemoryFault,
    InstructionLimitExceeded,
};

struct A32AeabiFinalizeResult {
    A32AeabiFinalizeError error{A32AeabiFinalizeError::None};
    std::size_t callbacks_completed{};
    std::optional<std::size_t> failing_record;
    std::optional<cpu::ExecutionResult> cpu_result;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32AeabiFinalizeError::None;
    }
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

    // Finalize pending records in reverse registration order. A null DSO
    // selector finalizes every pending record; otherwise only exact DSO-handle
    // matches are selected. Guest execution failure latches the affected
    // record Failed so later calls cannot replay partial side effects.
    [[nodiscard]] A32AeabiFinalizeResult finalize(
        memory::GuestMemory& memory,
        std::optional<std::uint32_t> dso_handle,
        const A32AeabiFinalizeOptions& options);

private:
    std::span<A32AeabiAtexitRecord> records_;
    std::size_t record_count_{};
};

[[nodiscard]] const char* to_string(
    A32AeabiFinalizeError error) noexcept;

}  // namespace liba32android::compat

#endif
