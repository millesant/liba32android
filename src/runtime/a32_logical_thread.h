#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cpu/a32_cpu.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::runtime {

// Engine-independent logical guest-thread identity. Zero is intentionally
// reserved as the invalid/unselected value.
class A32LogicalThreadId final {
public:
    constexpr A32LogicalThreadId() noexcept = default;

    [[nodiscard]] static constexpr std::optional<A32LogicalThreadId> from_raw(
        std::uint32_t value) noexcept {
        if (value == 0U) return std::nullopt;
        return A32LogicalThreadId{value};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != 0U;
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        const A32LogicalThreadId&,
        const A32LogicalThreadId&) noexcept = default;

private:
    explicit constexpr A32LogicalThreadId(std::uint32_t value) noexcept
        : value_(value) {}

    std::uint32_t value_{};
};

// One logical guest-thread identity paired with an existing engine-independent
// A32 execution request. This stores no scheduling or pthread/JNI policy.
struct A32LogicalExecutionContext {
    A32LogicalThreadId thread_id{};
    cpu::ExecutionRequest request{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread_id.valid() && request.instruction_count != 0U;
    }
};

[[nodiscard]] std::optional<A32LogicalExecutionContext>
make_a32_logical_execution_context(
    A32LogicalThreadId thread_id,
    cpu::ExecutionRequest request) noexcept;

// Preserve a logical thread identity while converting an existing suspended
// service result into its post-SVC continuation. Scheduling/wake policy stays
// with the caller/compatibility layer.
[[nodiscard]] std::optional<A32LogicalExecutionContext>
make_a32_service_resume_context(
    A32LogicalThreadId thread_id,
    const A32ServiceDispatchResult& suspended_result,
    std::size_t instruction_budget,
    std::optional<std::uint32_t> stop_pc = std::nullopt);

}  // namespace liba32android::runtime
