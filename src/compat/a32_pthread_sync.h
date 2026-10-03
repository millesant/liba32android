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
#define LIBA32ANDROID_A32_PTHREAD_KEY_CREATE_SVC 0x100
#define LIBA32ANDROID_A32_PTHREAD_KEY_DELETE_SVC 0x101
#define LIBA32ANDROID_A32_PTHREAD_GETSPECIFIC_SVC 0x102
#define LIBA32ANDROID_A32_PTHREAD_SETSPECIFIC_SVC 0x103
#define LIBA32ANDROID_A32_PTHREAD_COND_INIT_SVC 0x110
#define LIBA32ANDROID_A32_PTHREAD_COND_DESTROY_SVC 0x111
#define LIBA32ANDROID_A32_PTHREAD_COND_WAIT_SVC 0x112
#define LIBA32ANDROID_A32_PTHREAD_COND_TIMEDWAIT_SVC 0x113
#define LIBA32ANDROID_A32_PTHREAD_COND_SIGNAL_SVC 0x114
#define LIBA32ANDROID_A32_PTHREAD_COND_BROADCAST_SVC 0x115
#define LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_INIT_SVC 0x116
#define LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_DESTROY_SVC 0x117
#define LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_SETTYPE_SVC 0x118
#define LIBA32ANDROID_A32_PTHREAD_ONCE_SVC 0x119
#define LIBA32ANDROID_A32_PTHREAD_ONCE_COMPLETE_SVC 0x11A
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_INIT_SVC 0x11B
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_DESTROY_SVC 0x11C
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_RDLOCK_SVC 0x11D
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_TRYRDLOCK_SVC 0x11E
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_WRLOCK_SVC 0x11F
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_TRYWRLOCK_SVC 0x120
#define LIBA32ANDROID_A32_PTHREAD_RWLOCK_UNLOCK_SVC 0x121

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
inline constexpr std::uint32_t kA32PthreadKeyCreateSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_KEY_CREATE_SVC;
inline constexpr std::uint32_t kA32PthreadKeyDeleteSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_KEY_DELETE_SVC;
inline constexpr std::uint32_t kA32PthreadGetspecificSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_GETSPECIFIC_SVC;
inline constexpr std::uint32_t kA32PthreadSetspecificSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_SETSPECIFIC_SVC;
inline constexpr std::uint32_t kA32PthreadCondInitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_INIT_SVC;
inline constexpr std::uint32_t kA32PthreadCondDestroySvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_DESTROY_SVC;
inline constexpr std::uint32_t kA32PthreadCondWaitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_WAIT_SVC;
inline constexpr std::uint32_t kA32PthreadCondTimedwaitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_TIMEDWAIT_SVC;
inline constexpr std::uint32_t kA32PthreadCondSignalSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_SIGNAL_SVC;
inline constexpr std::uint32_t kA32PthreadCondBroadcastSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_COND_BROADCAST_SVC;
inline constexpr std::uint32_t kA32PthreadMutexattrInitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_INIT_SVC;
inline constexpr std::uint32_t kA32PthreadMutexattrDestroySvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_DESTROY_SVC;
inline constexpr std::uint32_t kA32PthreadMutexattrSettypeSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_MUTEXATTR_SETTYPE_SVC;
inline constexpr std::uint32_t kA32PthreadOnceSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ONCE_SVC;
inline constexpr std::uint32_t kA32PthreadOnceCompleteSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_ONCE_COMPLETE_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockInitSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_INIT_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockDestroySvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_DESTROY_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockRdlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_RDLOCK_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockTryrdlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_TRYRDLOCK_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockWrlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_WRLOCK_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockTrywrlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_TRYWRLOCK_SVC;
inline constexpr std::uint32_t kA32PthreadRwlockUnlockSvcImmediate =
    LIBA32ANDROID_A32_PTHREAD_RWLOCK_UNLOCK_SVC;

inline constexpr std::uint32_t kA32PthreadMutexNormal = 0U;
inline constexpr std::uint32_t kA32PthreadMutexRecursive = 1U;
inline constexpr std::uint32_t kA32PthreadMutexErrorcheck = 2U;
inline constexpr std::uint32_t kA32PthreadRecursiveDepthMax = 65536U;

inline constexpr std::uint32_t kA32AndroidSemValueMax = 0x7fffffffU;

enum class A32PthreadWaitKind : std::uint8_t {
    None = 0,
    Mutex,
    Semaphore,
    Condition,
    ConditionMutex,
    Once,
    RwRead,
    RwWrite,
};

enum class A32PthreadClockId : std::uint8_t {
    Realtime = 0,
    Monotonic = 1,
    MonotonicRaw = 4,
};

class A32PthreadClock {
public:
    virtual ~A32PthreadClock() = default;

    [[nodiscard]] virtual std::optional<std::int64_t> now_ns(
        A32PthreadClockId clock_id) const noexcept = 0;
};

struct A32PthreadMutexState {
    std::uint32_t address{};
    std::uint32_t owner_thread_id{};
    std::uint32_t recursion_depth{};
    std::uint32_t type{kA32PthreadMutexNormal};
};

struct A32PthreadMutexAttrState {
    std::uint32_t address{};
    std::uint32_t type{kA32PthreadMutexNormal};
    bool active{};
};

struct A32PthreadRwlockState {
    std::uint32_t address{};
    std::uint32_t writer_thread_id{};
    std::uint32_t reader_count{};
};

enum class A32PthreadOncePhase : std::uint8_t {
    Uninitialized = 0,
    Initializing,
    Done,
    Failed,
};

struct A32PthreadOnceState {
    std::uint32_t address{};
    std::uint32_t owner_thread_id{};
    std::uint32_t saved_post_svc_pc{};
    std::uint32_t saved_lr{};
    std::uint32_t saved_cpsr{};
    std::uint64_t sequence{};
    A32PthreadOncePhase phase{A32PthreadOncePhase::Uninitialized};
};

struct A32PthreadSyncOptions {
    // Guest address (low bit selects Thumb) of an internal trampoline whose
    // first instruction is SVC kA32PthreadOnceCompleteSvcImmediate.
    std::uint32_t once_completion_trampoline{};
};

struct A32SemaphoreState {
    std::uint32_t address{};
    std::uint32_t value{};
};

struct A32PthreadWaiter {
    A32PthreadWaitKind kind{A32PthreadWaitKind::None};
    std::uint32_t object_address{};
    std::uint32_t mutex_address{};
    std::uint32_t thread_id{};
    std::uint32_t result_value{};
    std::int64_t deadline_ns{};
    std::uint64_t sequence{};
    bool timed{};
    bool ready{};
};

struct A32PthreadWake {
    A32PthreadWaitKind kind{A32PthreadWaitKind::None};
    std::uint32_t object_address{};
    std::uint32_t mutex_address{};
    std::uint32_t thread_id{};
    std::uint32_t result_value{};
};

struct A32PthreadKeyState {
    std::uint32_t key{};
    std::uint32_t destructor{};
    bool active{};
};

struct A32PthreadTlsValue {
    std::uint32_t key{};
    std::uint32_t thread_id{};
    std::uint32_t value{};
    bool active{};
};

struct A32PthreadTlsDestructorCall {
    std::uint32_t key{};
    std::uint32_t destructor{};
    std::uint32_t value{};
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
        std::span<A32PthreadWaiter> waiters,
        std::span<A32PthreadKeyState> keys = {},
        std::span<A32PthreadTlsValue> tls_values = {},
        A32PthreadClock* clock = nullptr,
        std::span<A32PthreadMutexAttrState> mutex_attrs = {},
        std::span<A32PthreadRwlockState> rwlocks = {},
        std::span<A32PthreadOnceState> once_controls = {},
        A32PthreadSyncOptions options = {}) noexcept;

    A32PthreadSyncService(const A32PthreadSyncService&) = delete;
    A32PthreadSyncService& operator=(const A32PthreadSyncService&) = delete;
    A32PthreadSyncService(A32PthreadSyncService&&) = delete;
    A32PthreadSyncService& operator=(A32PthreadSyncService&&) = delete;

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

    [[nodiscard]] runtime::A32LogicalThreadId logical_thread_id() const noexcept {
        return current_thread_id_;
    }

    [[nodiscard]] std::optional<A32PthreadWake> pop_ready() noexcept;

    // Evaluate absolute condition-variable deadlines against the injected
    // clock. Expired waiters move into the existing mutex-reacquire path;
    // they do not become resumable until that mutex is owned again.
    [[nodiscard]] bool poll_condition_timeouts() noexcept;

    [[nodiscard]] std::optional<std::int64_t>
    next_condition_deadline_ns() const noexcept;

    // Latch every pthread_once initializer currently owned by this logical
    // thread as Failed. The embedding calls this if guest execution faults
    // before reaching the internal once-completion trampoline.
    [[nodiscard]] bool fail_once_initialization(
        std::uint32_t thread_id) noexcept;

    [[nodiscard]] std::size_t tls_key_slot_count() const noexcept {
        return keys_.size();
    }

    // Consume one current value before a thread-exit destructor callback.
    // Clearing happens before publication so a callback sees NULL unless it
    // explicitly repopulates the key, matching pthread exit semantics.
    [[nodiscard]] std::optional<A32PthreadTlsDestructorCall>
    take_tls_destructor(
        std::uint32_t thread_id,
        std::size_t key_slot) noexcept;

    void clear_tls_values_for_thread(
        std::uint32_t thread_id) noexcept;

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    [[nodiscard]] bool sync_configuration_valid() const noexcept;
    [[nodiscard]] bool tls_configuration_valid() const noexcept;
    [[nodiscard]] std::size_t find_key(std::uint32_t key) const noexcept;
    [[nodiscard]] std::size_t allocate_key(std::uint32_t destructor) noexcept;
    [[nodiscard]] std::size_t find_tls_value(
        std::uint32_t key,
        std::uint32_t thread_id) const noexcept;
    [[nodiscard]] std::size_t allocate_tls_value(
        std::uint32_t key,
        std::uint32_t thread_id,
        std::uint32_t value) noexcept;
    [[nodiscard]] std::size_t find_mutex(std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_mutex(std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_mutex_attr(
        std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_mutex_attr(
        std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_rwlock(
        std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_rwlock(
        std::uint32_t address) noexcept;
    [[nodiscard]] std::size_t find_once(
        std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t ensure_once(
        std::uint32_t address) noexcept;
    [[nodiscard]] std::optional<std::size_t> current_once_owner() const noexcept;
    [[nodiscard]] std::size_t find_semaphore(std::uint32_t address) const noexcept;
    [[nodiscard]] std::size_t allocate_semaphore(
        std::uint32_t address,
        std::uint32_t value) noexcept;
    [[nodiscard]] bool has_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address) const noexcept;
    [[nodiscard]] bool has_mutex_waiter(
        std::uint32_t mutex_address) const noexcept;
    [[nodiscard]] bool enqueue_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address) noexcept;
    [[nodiscard]] std::optional<std::size_t> enqueue_condition_waiter(
        std::uint32_t condition_address,
        std::uint32_t mutex_address,
        std::optional<std::int64_t> deadline_ns) noexcept;
    [[nodiscard]] std::optional<std::size_t> oldest_waiter(
        A32PthreadWaitKind kind,
        std::uint32_t object_address,
        bool ready) const noexcept;
    [[nodiscard]] std::optional<std::size_t> oldest_mutex_waiter(
        std::uint32_t mutex_address) const noexcept;
    [[nodiscard]] std::optional<std::size_t> oldest_condition_waiter(
        std::uint32_t condition_address) const noexcept;
    [[nodiscard]] bool release_mutex(
        std::uint32_t mutex_address,
        std::uint32_t owner_thread_id) noexcept;
    [[nodiscard]] bool transfer_condition_waiter(
        std::size_t waiter_index,
        std::uint32_t result_value) noexcept;
    [[nodiscard]] bool process_expired_condition_waiters(
        std::int64_t now_ns) noexcept;
    void grant_rwlock_waiters(
        std::size_t rwlock_index) noexcept;
    void wake_once_waiters(
        std::uint32_t once_address) noexcept;

    std::span<A32PthreadMutexState> mutexes_;
    std::span<A32SemaphoreState> semaphores_;
    std::span<A32PthreadWaiter> waiters_;
    std::span<A32PthreadKeyState> keys_;
    std::span<A32PthreadTlsValue> tls_values_;
    A32PthreadClock* clock_{};
    std::span<A32PthreadMutexAttrState> mutex_attrs_;
    std::span<A32PthreadRwlockState> rwlocks_;
    std::span<A32PthreadOnceState> once_controls_;
    A32PthreadSyncOptions options_{};
    runtime::A32LogicalThreadId current_thread_id_{};
    std::uint64_t next_sequence_{1};
    std::uint64_t next_once_sequence_{1};
};

}  // namespace liba32android::compat

#endif
