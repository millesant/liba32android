#pragma once

#define LIBA32ANDROID_A32_PTHREAD_ATTR_INIT_SVC 0x104
#define LIBA32ANDROID_A32_PTHREAD_ATTR_DESTROY_SVC 0x105
#define LIBA32ANDROID_A32_PTHREAD_ATTR_GETDETACHSTATE_SVC 0x106
#define LIBA32ANDROID_A32_PTHREAD_ATTR_SETDETACHSTATE_SVC 0x107
#define LIBA32ANDROID_A32_PTHREAD_ATTR_GETSTACKSIZE_SVC 0x108
#define LIBA32ANDROID_A32_PTHREAD_ATTR_SETSTACKSIZE_SVC 0x109
#define LIBA32ANDROID_A32_PTHREAD_CREATE_SVC 0x10A
#define LIBA32ANDROID_A32_PTHREAD_SELF_SVC 0x10B
#define LIBA32ANDROID_A32_PTHREAD_EQUAL_SVC 0x10C
#define LIBA32ANDROID_A32_PTHREAD_EXIT_SVC 0x10D
#define LIBA32ANDROID_A32_PTHREAD_JOIN_SVC 0x10E
#define LIBA32ANDROID_A32_PTHREAD_DETACH_SVC 0x10F

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "compat/a32_android_errno.h"
#include "compat/a32_pthread_sync.h"
#include "runtime/a32_logical_thread.h"
#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32PthreadAttrInitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_INIT_SVC;
inline constexpr std::uint32_t kA32PthreadAttrDestroySvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_DESTROY_SVC;
inline constexpr std::uint32_t kA32PthreadAttrGetdetachstateSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_GETDETACHSTATE_SVC;
inline constexpr std::uint32_t kA32PthreadAttrSetdetachstateSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_SETDETACHSTATE_SVC;
inline constexpr std::uint32_t kA32PthreadAttrGetstacksizeSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_GETSTACKSIZE_SVC;
inline constexpr std::uint32_t kA32PthreadAttrSetstacksizeSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ATTR_SETSTACKSIZE_SVC;
inline constexpr std::uint32_t kA32PthreadCreateSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_CREATE_SVC;
inline constexpr std::uint32_t kA32PthreadSelfSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_SELF_SVC;
inline constexpr std::uint32_t kA32PthreadEqualSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_EQUAL_SVC;
inline constexpr std::uint32_t kA32PthreadExitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_EXIT_SVC;
inline constexpr std::uint32_t kA32PthreadJoinSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_JOIN_SVC;
inline constexpr std::uint32_t kA32PthreadDetachSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_DETACH_SVC;

inline constexpr std::uint32_t kA32PthreadCreateJoinable = 0U;
inline constexpr std::uint32_t kA32PthreadCreateDetached = 1U;
inline constexpr std::uint32_t kA32PthreadDefaultStackSize = 1024U * 1024U;
inline constexpr std::uint32_t kA32PthreadDestructorIterations = 4U;

struct A32PthreadAttrState {
    std::uint32_t address{};
    std::uint32_t stack_size{};
    bool detached{};
    bool active{};
};

enum class A32PthreadThreadPhase : std::uint8_t {
    Free = 0,
    Running,
    Cleaning,
    Exited,
    CleanupFailed,
};

struct A32PthreadThreadState {
    std::uint32_t pthread_id{};
    std::uint32_t start_routine{};
    std::uint32_t argument{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::uint32_t return_value{};
    std::uint32_t joiner_thread_id{};
    std::uint32_t join_result_address{};
    bool detached{};
    bool owns_stack{};
    bool initial_thread{};
    bool start_pending{};
    bool join_claimed{};
    bool join_wake_ready{};
    std::uint64_t sequence{};
    std::uint64_t join_wake_sequence{};
    A32PthreadThreadPhase phase{A32PthreadThreadPhase::Free};
    runtime::A32LogicalExecutionContext context{};
};

struct A32PthreadCreatedThread {
    std::uint32_t pthread_id{};
    bool detached{};
    runtime::A32LogicalExecutionContext context{};
};

struct A32PthreadJoinWake {
    std::uint32_t thread_id{};
    std::uint32_t target_thread_id{};
};

enum class A32PthreadExitCleanupError : std::uint8_t {
    None = 0,
    InvalidStack,
    InvalidDestructorAddress,
    MemoryFault,
    CpuException,
    ServiceLimitExceeded,
    ServiceUnhandled,
    ServiceFailed,
    ServiceSuspended,
    InstructionLimitExceeded,
    ExitHookFailed,
    JoinResultWriteFailed,
};

struct A32PthreadExitCleanupResult {
    A32PthreadExitCleanupError error{A32PthreadExitCleanupError::None};
    std::uint32_t thread_id{};
    std::uint32_t callbacks_completed{};
    std::uint32_t iterations_completed{};
    std::optional<std::uint32_t> failing_key;
    std::optional<std::uint32_t> failing_svc_immediate;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == A32PthreadExitCleanupError::None;
    }
};

// Compatibility-layer seam for later per-thread cleanup such as JNI local
// references/attachment state. It runs after pthread TLS destructors and before
// the thread is published Exited/woken/reclaimed.
class A32PthreadThreadExitHook {
public:
    virtual ~A32PthreadThreadExitHook() = default;

    [[nodiscard]] virtual bool on_thread_exit(
        memory::GuestMemory& memory,
        runtime::A32LogicalThreadId thread_id) = 0;
};

struct A32PthreadLifecycleOptions {
    std::uint32_t stack_arena_base{};
    std::uint32_t stack_arena_size{};
    std::uint32_t page_size{4096U};
    std::uint32_t default_stack_size{kA32PthreadDefaultStackSize};
    std::uint32_t exit_trampoline{};
    std::size_t thread_instruction_budget{65536U};
    std::uint32_t first_thread_id{2U};

    // Required only when a TLS sync service is supplied. Destructor callbacks
    // run on the exiting guest stack and return to this normalized stop PC.
    std::uint32_t tls_destructor_return_pc{};
    std::size_t tls_destructor_instruction_budget{};
    std::uint32_t tls_destructor_service_limit{};
};

// Bounded logical pthread lifecycle state. pthread_t intentionally uses the
// same opaque non-zero numeric identity as A32LogicalThreadId inside this
// internal compatibility model; that equivalence is not a public C ABI.
class A32PthreadLifecycleService final
    : public runtime::A32HostServiceHandler {
public:
    A32PthreadLifecycleService(
        A32PthreadLifecycleOptions options,
        std::span<A32PthreadAttrState> attrs,
        std::span<A32PthreadThreadState> threads,
        A32PthreadSyncService* sync_service = nullptr,
        A32PthreadThreadExitHook* exit_hook = nullptr) noexcept;

    A32PthreadLifecycleService(const A32PthreadLifecycleService&) = delete;
    A32PthreadLifecycleService& operator=(const A32PthreadLifecycleService&) = delete;
    A32PthreadLifecycleService(A32PthreadLifecycleService&&) = delete;
    A32PthreadLifecycleService& operator=(A32PthreadLifecycleService&&) = delete;

    [[nodiscard]] bool register_initial_thread(
        runtime::A32LogicalThreadId thread_id) noexcept;

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

    [[nodiscard]] std::optional<A32PthreadCreatedThread>
    pop_created_thread() noexcept;

    // Pop the oldest completed blocked join. The target remains owned until
    // this scheduler handoff is consumed; popping reclaims its thread/stack
    // metadata before the joiner resumes after the trapped pthread_join SVC.
    [[nodiscard]] std::optional<A32PthreadJoinWake>
    pop_ready_join() noexcept;

    [[nodiscard]] const std::optional<A32PthreadExitCleanupResult>&
    last_exit_cleanup_result() const noexcept {
        return last_exit_cleanup_result_;
    }

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] std::size_t find_attr(std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_attr(std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_thread(std::uint32_t pthread_id) const noexcept;
    [[nodiscard]] std::size_t free_thread_slot() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> choose_thread_id() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> choose_stack_base(
        std::uint32_t requested_size,
        std::uint32_t& allocated_size) const noexcept;
    [[nodiscard]] std::optional<runtime::A32LogicalExecutionContext>
    make_thread_context(
        std::uint32_t pthread_id,
        std::uint32_t start_routine,
        std::uint32_t argument,
        std::uint32_t stack_base,
        std::uint32_t stack_size) const noexcept;
    [[nodiscard]] A32PthreadExitCleanupResult run_exit_cleanup(
        memory::GuestMemory& memory,
        A32PthreadThreadState& thread,
        std::uint32_t stack_pointer);
    void reclaim_thread(std::size_t index) noexcept;

    A32PthreadLifecycleOptions options_;
    std::span<A32PthreadAttrState> attrs_;
    std::span<A32PthreadThreadState> threads_;
    A32PthreadSyncService* sync_service_{};
    A32PthreadThreadExitHook* exit_hook_{};
    runtime::A32LogicalThreadId current_thread_id_{};
    std::uint32_t next_thread_id_{};
    std::uint64_t next_sequence_{1U};
    std::uint64_t next_join_wake_sequence_{1U};
    std::optional<A32PthreadExitCleanupResult> last_exit_cleanup_result_;
};

[[nodiscard]] const char* to_string(
    A32PthreadExitCleanupError error) noexcept;

}  // namespace liba32android::compat

#endif
