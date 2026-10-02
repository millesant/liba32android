#pragma once

#define LIBA32ANDROID_A32_RAISE_SVC 0x127
#define LIBA32ANDROID_A32_SIGACTION_SVC 0x128
#define LIBA32ANDROID_A32_SIGPENDING_SVC 0x129
#define LIBA32ANDROID_A32_PTHREAD_SIGMASK_SVC 0x12A
#define LIBA32ANDROID_A32_SIGWAIT_SVC 0x12B

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "compat/a32_libc_integer.h"
#include "compat/a32_pthread_lifecycle.h"
#include "runtime/a32_logical_thread.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32RaiseSvcImmediate =
    LIBA32ANDROID_A32_RAISE_SVC;
inline constexpr std::uint32_t kA32SigactionSvcImmediate =
    LIBA32ANDROID_A32_SIGACTION_SVC;
inline constexpr std::uint32_t kA32SigpendingSvcImmediate =
    LIBA32ANDROID_A32_SIGPENDING_SVC;
inline constexpr std::uint32_t kA32PthreadSigmaskSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_SIGMASK_SVC;
inline constexpr std::uint32_t kA32SigwaitSvcImmediate =
    LIBA32ANDROID_A32_SIGWAIT_SVC;

inline constexpr std::uint32_t kA32SignalSetBits = 32U;
inline constexpr std::uint32_t kA32SignalMaxPublic = 31U;
inline constexpr std::uint32_t kA32SignalSupportedMask = 0x7fffffffU;
inline constexpr std::uint32_t kA32SignalDefault = 0U;
inline constexpr std::uint32_t kA32SignalIgnore = 1U;

inline constexpr std::uint32_t kA32SigBlock = 0U;
inline constexpr std::uint32_t kA32SigUnblock = 1U;
inline constexpr std::uint32_t kA32SigSetmask = 2U;

inline constexpr std::uint32_t kA32SigFpe = 8U;
inline constexpr std::uint32_t kA32SigKill = 9U;
inline constexpr std::uint32_t kA32SigPipe = 13U;
inline constexpr std::uint32_t kA32SigChld = 17U;
inline constexpr std::uint32_t kA32SigCont = 18U;
inline constexpr std::uint32_t kA32SigStop = 19U;
inline constexpr std::uint32_t kA32SigTstp = 20U;
inline constexpr std::uint32_t kA32SigTtin = 21U;
inline constexpr std::uint32_t kA32SigTtou = 22U;
inline constexpr std::uint32_t kA32SigUrg = 23U;
inline constexpr std::uint32_t kA32SigWinch = 28U;

[[nodiscard]] constexpr std::uint32_t a32_signal_bit(
    std::uint32_t signal) noexcept {
    return signal >= 1U && signal <= kA32SignalSetBits
        ? (1U << (signal - 1U))
        : 0U;
}

struct A32SignalActionState {
    std::uint32_t handler{};
    std::uint32_t mask{};
    std::uint32_t flags{};
    std::uint32_t restorer{};
};

struct A32SignalWaiter {
    std::uint32_t thread_id{};
    std::uint32_t mask{};
    std::uint32_t signal_out_address{};
    std::uint32_t delivered_signal{};
    std::uint32_t result_value{};
    std::uint64_t sequence{};
    bool active{};
    bool ready{};
};

struct A32SignalWake {
    std::uint32_t thread_id{};
    std::uint32_t signal{};
    std::uint32_t result_value{};
};

enum class A32SignalBoundaryError : std::uint8_t {
    None = 0,
    DefaultFatal,
    DefaultStopUnsupported,
    DefaultContinueUnsupported,
    CustomHandlerUnsupported,
    WaiterCapacityExceeded,
};

class A32SignalService final : public runtime::A32HostServiceHandler {
public:
    A32SignalService(
        std::span<A32PthreadThreadState> threads,
        std::span<A32SignalActionState> actions,
        std::span<A32SignalWaiter> waiters,
        A32LibcErrnoSink& errno_sink) noexcept;

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

    [[nodiscard]] std::optional<A32SignalWake> pop_ready() noexcept;

    // Inject one process-directed logical signal. This is a bounded test /
    // embedding seam, not host signal passthrough.
    [[nodiscard]] bool queue_process_signal(
        memory::GuestMemory& memory,
        std::uint32_t signal) noexcept;

    [[nodiscard]] std::uint32_t process_pending() const noexcept {
        return process_pending_;
    }

    [[nodiscard]] A32SignalBoundaryError last_boundary_error() const noexcept {
        return last_boundary_error_;
    }

    [[nodiscard]] std::uint32_t last_boundary_signal() const noexcept {
        return last_boundary_signal_;
    }

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] std::size_t find_thread(std::uint32_t thread_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> oldest_waiter(
        std::uint32_t signal_bit) const noexcept;
    [[nodiscard]] bool disposition_ignored(std::uint32_t signal) const noexcept;
    [[nodiscard]] A32SignalBoundaryError classify_unblocked(
        std::uint32_t signal) const noexcept;
    void clear_pending_signal(std::uint32_t signal) noexcept;
    [[nodiscard]] bool resolve_unblocked_pending(
        A32PthreadThreadState& thread) noexcept;

    std::span<A32PthreadThreadState> threads_;
    std::span<A32SignalActionState> actions_;
    std::span<A32SignalWaiter> waiters_;
    A32LibcErrnoSink& errno_sink_;
    runtime::A32LogicalThreadId current_thread_id_{};
    std::uint32_t process_pending_{};
    std::uint64_t next_sequence_{1U};
    A32SignalBoundaryError last_boundary_error_{A32SignalBoundaryError::None};
    std::uint32_t last_boundary_signal_{};
};

[[nodiscard]] const char* to_string(A32SignalBoundaryError error) noexcept;

}  // namespace liba32android::compat

#endif
