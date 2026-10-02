#pragma once

#define LIBA32ANDROID_A32_SETPRIORITY_SVC 0x12C
#define LIBA32ANDROID_A32_SCHED_GET_PRIORITY_MAX_SVC 0x12D
#define LIBA32ANDROID_A32_SCHED_GET_PRIORITY_MIN_SVC 0x12E
#define LIBA32ANDROID_A32_SCHED_GETAFFINITY_SVC 0x12F
#define LIBA32ANDROID_A32_SCHED_SETSCHEDULER_SVC 0x130
#define LIBA32ANDROID_A32_SCHED_YIELD_SVC 0x131

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "compat/a32_libc_integer.h"
#include "compat/a32_pthread_lifecycle.h"
#include "runtime/a32_logical_thread.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32SetprioritySvcImmediate =
    LIBA32ANDROID_A32_SETPRIORITY_SVC;
inline constexpr std::uint32_t kA32SchedGetPriorityMaxSvcImmediate =
    LIBA32ANDROID_A32_SCHED_GET_PRIORITY_MAX_SVC;
inline constexpr std::uint32_t kA32SchedGetPriorityMinSvcImmediate =
    LIBA32ANDROID_A32_SCHED_GET_PRIORITY_MIN_SVC;
inline constexpr std::uint32_t kA32SchedGetaffinitySvcImmediate =
    LIBA32ANDROID_A32_SCHED_GETAFFINITY_SVC;
inline constexpr std::uint32_t kA32SchedSetschedulerSvcImmediate =
    LIBA32ANDROID_A32_SCHED_SETSCHEDULER_SVC;
inline constexpr std::uint32_t kA32SchedYieldSvcImmediate =
    LIBA32ANDROID_A32_SCHED_YIELD_SVC;

inline constexpr std::int32_t kA32PrioProcess = 0;
inline constexpr std::int32_t kA32SchedFifo = 1;
inline constexpr std::int32_t kA32SchedRr = 2;
inline constexpr std::int32_t kA32SchedBatch = 3;
inline constexpr std::int32_t kA32SchedIdle = 5;
inline constexpr std::int32_t kA32SchedPriorityMinRealtime = 1;
inline constexpr std::int32_t kA32SchedPriorityMaxRealtime = 99;
inline constexpr std::int32_t kA32NiceMin = -20;
inline constexpr std::int32_t kA32NiceMax = 19;
inline constexpr std::uint32_t kA32CpuSetBytes = 4U;

struct A32SchedulerOptions {
    std::uint32_t logical_cpu_count{1U};
};

class A32SchedulerService final : public runtime::A32HostServiceHandler {
public:
    A32SchedulerService(
        std::span<A32PthreadThreadState> threads,
        A32LibcErrnoSink& errno_sink,
        A32SchedulerOptions options = {}) noexcept
        : threads_(threads),
          errno_sink_(errno_sink),
          options_(options) {}

    void set_current_thread_id(std::uint32_t thread_id) noexcept {
        current_thread_id_ =
            runtime::A32LogicalThreadId::from_raw(thread_id).value_or(
                runtime::A32LogicalThreadId{});
    }

    [[nodiscard]] bool set_current_thread_id(
        runtime::A32LogicalThreadId thread_id) noexcept {
        if (!thread_id.valid()) return false;
        current_thread_id_ = thread_id;
        return true;
    }

    [[nodiscard]] bool set_current_thread_context(
        const runtime::A32LogicalExecutionContext& context) noexcept {
        if (!context.valid()) return false;
        current_thread_id_ = context.thread_id;
        return true;
    }

    [[nodiscard]] std::uint32_t current_thread_id() const noexcept {
        return current_thread_id_.value();
    }

    [[nodiscard]] A32SchedulerOptions options() const noexcept {
        return options_;
    }

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] std::size_t find_thread(std::uint32_t thread_id) const noexcept;

    std::span<A32PthreadThreadState> threads_;
    A32LibcErrnoSink& errno_sink_;
    A32SchedulerOptions options_;
    runtime::A32LogicalThreadId current_thread_id_{};
};

}  // namespace liba32android::compat

#endif
