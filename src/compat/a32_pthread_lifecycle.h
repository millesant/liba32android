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

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "compat/a32_android_errno.h"
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

inline constexpr std::uint32_t kA32PthreadCreateJoinable = 0U;
inline constexpr std::uint32_t kA32PthreadCreateDetached = 1U;
inline constexpr std::uint32_t kA32PthreadDefaultStackSize = 1024U * 1024U;

struct A32PthreadAttrState {
    std::uint32_t address{};
    std::uint32_t stack_size{};
    bool detached{};
    bool active{};
};

enum class A32PthreadThreadPhase : std::uint8_t {
    Free = 0,
    Running,
    Exited,
};

struct A32PthreadThreadState {
    std::uint32_t pthread_id{};
    std::uint32_t start_routine{};
    std::uint32_t argument{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::uint32_t return_value{};
    bool detached{};
    bool owns_stack{};
    bool initial_thread{};
    bool start_pending{};
    std::uint64_t sequence{};
    A32PthreadThreadPhase phase{A32PthreadThreadPhase::Free};
    runtime::A32LogicalExecutionContext context{};
};

struct A32PthreadCreatedThread {
    std::uint32_t pthread_id{};
    bool detached{};
    runtime::A32LogicalExecutionContext context{};
};

struct A32PthreadLifecycleOptions {
    std::uint32_t stack_arena_base{};
    std::uint32_t stack_arena_size{};
    std::uint32_t page_size{4096U};
    std::uint32_t default_stack_size{kA32PthreadDefaultStackSize};
    std::uint32_t exit_trampoline{};
    std::size_t thread_instruction_budget{65536U};
    std::uint32_t first_thread_id{2U};
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
        std::span<A32PthreadThreadState> threads) noexcept;

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
    [[nodiscard]] std::optional<std::uint32_t> allocate_thread_id() noexcept;
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

    A32PthreadLifecycleOptions options_;
    std::span<A32PthreadAttrState> attrs_;
    std::span<A32PthreadThreadState> threads_;
    runtime::A32LogicalThreadId current_thread_id_{};
    std::uint32_t next_thread_id_{};
    std::uint64_t next_sequence_{1U};
};

}  // namespace liba32android::compat

#endif
