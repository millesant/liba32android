#pragma once

#define LIBA32ANDROID_A32_PTHREAD_MUTEX_INIT_SVC 0xB3
#define LIBA32ANDROID_A32_PTHREAD_MUTEX_DESTROY_SVC 0xB4
#define LIBA32ANDROID_A32_PTHREAD_MUTEX_LOCK_SVC 0xB5
#define LIBA32ANDROID_A32_PTHREAD_MUTEX_TRYLOCK_SVC 0xB6
#define LIBA32ANDROID_A32_PTHREAD_MUTEX_UNLOCK_SVC 0xB7
#define LIBA32ANDROID_A32_SEM_INIT_SVC 0xB8
#define LIBA32ANDROID_A32_SEM_DESTROY_SVC 0xB9
#define LIBA32ANDROID_A32_SEM_WAIT_SVC 0xBA
#define LIBA32ANDROID_A32_SEM_POST_SVC 0xBB

#ifdef __cplusplus

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

inline constexpr std::uint32_t kA32PthreadMutexInitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEX_INIT_SVC;
inline constexpr std::uint32_t kA32PthreadMutexDestroySvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEX_DESTROY_SVC;
inline constexpr std::uint32_t kA32PthreadMutexLockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEX_LOCK_SVC;
inline constexpr std::uint32_t kA32PthreadMutexTrylockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEX_TRYLOCK_SVC;
inline constexpr std::uint32_t kA32PthreadMutexUnlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEX_UNLOCK_SVC;
inline constexpr std::uint32_t kA32SemInitSvcImmediate =
    LIBA32ANDROID_A32_SEM_INIT_SVC;
inline constexpr std::uint32_t kA32SemDestroySvcImmediate =
    LIBA32ANDROID_A32_SEM_DESTROY_SVC;
inline constexpr std::uint32_t kA32SemWaitSvcImmediate =
    LIBA32ANDROID_A32_SEM_WAIT_SVC;
inline constexpr std::uint32_t kA32SemPostSvcImmediate =
    LIBA32ANDROID_A32_SEM_POST_SVC;

inline constexpr std::int32_t kA32AndroidEbusy = 16;
inline constexpr std::uint32_t kA32AndroidSemValueMax = 0x7fffffffU;

enum class A32PthreadWaitKind : std::uint8_t {
    None = 0,
    Mutex,
    Semaphore,
};

struct A32PthreadMutexState {
    std::uint32_t address{};
    std::uint32_t owner_thread_id{};
};

struct A32SemaphoreState {
    std::uint32_t address{};
    std::uint32_t value{};
};

struct A32PthreadWaiter {
    A32PthreadWaitKind kind{A32PthreadWaitKind::None};
    std::uint32_t object_address{};
    std::uint32_t thread_id{};
    std::uint64_t sequence{};
    bool ready{};
};

struct A32PthreadWake {
    A32PthreadWaitKind kind{A32PthreadWaitKind::None};
    std::uint32_t object_address{};
    std::uint32_t thread_id{};
};

// Bounded compatibility-side state for opaque guest mutex/semaphore addresses.
// The embedding selects current_thread_id before executing each guest thread.
// Contended lock/wait calls return Suspended and are granted by a later
// unlock/post before the embedding resumes that thread after its trapped SVC.
class A32PthreadSyncService final
    : public runtime::A32HostServiceHandler {
public:
    A32PthreadSyncService(
        std::span<A32PthreadMutexState> mutexes,
        std::span<A32SemaphoreState> semaphores,
        std::span<A32PthreadWaiter> waiters) noexcept;

    A32PthreadSyncService(const A32PthreadSyncService&) = delete;
    A32PthreadSyncService& operator=(const A32PthreadSyncService&) = delete;
    A32PthreadSyncService(A32PthreadSyncService&&) = delete;
    A32PthreadSyncService& operator=(A32PthreadSyncService&&) = delete;

    void set_current_thread_id(std::uint32_t thread_id) noexcept {
        current_thread_id_ = thread_id;
    }

    [[nodiscard]] std::uint32_t current_thread_id() const noexcept {
        return current_thread_id_;
    }

    [[nodiscard]] std::optional<A32PthreadWake> pop_ready() noexcept;

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    [[nodiscard]] bool configuration_valid() const noexcept;
    [[nodiscard]] std::size_t find_mutex(std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_mutex(std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_semaphore(std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t allocate_semaphore(
        std::uint32_t address,
        std::uint32_t value) noexcept;
    [[nodiscard]] bool has_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address) const noexcept;
    [[nodiscard]] bool enqueue_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address) noexcept;
    [[nodiscard]] std::optional<std::size_t> oldest_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address,
        bool ready) const noexcept;

    std::span<A32PthreadMutexState> mutexes_;
    std::span<A32SemaphoreState> semaphores_;
    std::span<A32PthreadWaiter> waiters_;
    std::uint32_t current_thread_id_{};
    std::uint64_t next_sequence_{1};
};

}  // namespace liba32android::compat

#endif
