#include "compat/a32_pthread_sync.h"

#include <array>

#include "memory/guest_memory.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

[[nodiscard]] bool is_sync_svc(std::uint32_t svc) noexcept {
    switch (svc) {
    case kA32PthreadMutexInitSvcImmediate:
    case kA32PthreadMutexDestroySvcImmediate:
    case kA32PthreadMutexLockSvcImmediate:
    case kA32PthreadMutexTrylockSvcImmediate:
    case kA32PthreadMutexUnlockSvcImmediate:
    case kA32SemInitSvcImmediate:
    case kA32SemDestroySvcImmediate:
    case kA32SemWaitSvcImmediate:
    case kA32SemPostSvcImmediate:
    case kA32PthreadKeyCreateSvcImmediate:
    case kA32PthreadKeyDeleteSvcImmediate:
    case kA32PthreadGetspecificSvcImmediate:
    case kA32PthreadSetspecificSvcImmediate:
    case kA32PthreadCondInitSvcImmediate:
    case kA32PthreadCondDestroySvcImmediate:
    case kA32PthreadCondWaitSvcImmediate:
    case kA32PthreadCondTimedwaitSvcImmediate:
    case kA32PthreadCondSignalSvcImmediate:
    case kA32PthreadCondBroadcastSvcImmediate:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool write_u32_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    }};
    return memory.write(address, bytes);
}

}  // namespace

A32PthreadSyncService::A32PthreadSyncService(
    std::span<A32PthreadMutexState> mutexes,
    std::span<A32SemaphoreState> semaphores,
    std::span<A32PthreadWaiter> waiters,
    std::span<A32PthreadKeyState> keys,
    std::span<A32PthreadTlsValue> tls_values,
    A32PthreadClock* clock) noexcept
    : mutexes_(mutexes),
      semaphores_(semaphores),
      waiters_(waiters),
      keys_(keys),
      tls_values_(tls_values),
      clock_(clock) {
    for (auto& mutex : mutexes_) mutex = {};
    for (auto& semaphore : semaphores_) semaphore = {};
    for (auto& waiter : waiters_) waiter = {};
    for (auto& key : keys_) key = {};
    for (auto& value : tls_values_) value = {};
}

bool A32PthreadSyncService::sync_configuration_valid() const noexcept {
    return current_thread_id_.valid() &&
           !mutexes_.empty() &&
           !semaphores_.empty() &&
           !waiters_.empty();
}

bool A32PthreadSyncService::tls_configuration_valid() const noexcept {
    return current_thread_id_.valid() &&
           !keys_.empty() &&
           !tls_values_.empty();
}

std::size_t A32PthreadSyncService::find_key(
    std::uint32_t key) const noexcept {
    for (std::size_t index = 0; index < keys_.size(); ++index) {
        if (keys_[index].active && keys_[index].key == key) {
            return index;
        }
    }
    return keys_.size();
}

std::size_t A32PthreadSyncService::allocate_key(
    std::uint32_t destructor) noexcept {
    for (std::size_t index = 0; index < keys_.size(); ++index) {
        if (keys_[index].active) continue;
        const std::uint64_t key64 =
            static_cast<std::uint64_t>(index) + 1U;
        if (key64 > std::numeric_limits<std::uint32_t>::max()) {
            return keys_.size();
        }
        keys_[index] = A32PthreadKeyState{
            .key = static_cast<std::uint32_t>(key64),
            .destructor = destructor,
            .active = true,
        };
        return index;
    }
    return keys_.size();
}

std::size_t A32PthreadSyncService::find_tls_value(
    std::uint32_t key,
    std::uint32_t thread_id) const noexcept {
    for (std::size_t index = 0; index < tls_values_.size(); ++index) {
        if (tls_values_[index].active &&
            tls_values_[index].key == key &&
            tls_values_[index].thread_id == thread_id) {
            return index;
        }
    }
    return tls_values_.size();
}

std::size_t A32PthreadSyncService::allocate_tls_value(
    std::uint32_t key,
    std::uint32_t thread_id,
    std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < tls_values_.size(); ++index) {
        if (tls_values_[index].active) continue;
        tls_values_[index] = A32PthreadTlsValue{
            .key = key,
            .thread_id = thread_id,
            .value = value,
            .active = true,
        };
        return index;
    }
    return tls_values_.size();
}

std::optional<A32PthreadTlsDestructorCall>
A32PthreadSyncService::take_tls_destructor(
    std::uint32_t thread_id,
    std::size_t key_slot) noexcept {
    if (thread_id == 0U || key_slot >= keys_.size()) {
        return std::nullopt;
    }
    const auto& key = keys_[key_slot];
    if (!key.active || key.destructor == 0U) {
        return std::nullopt;
    }

    const std::size_t value_index =
        find_tls_value(key.key, thread_id);
    if (value_index >= tls_values_.size() ||
        tls_values_[value_index].value == 0U) {
        return std::nullopt;
    }

    const A32PthreadTlsDestructorCall call{
        .key = key.key,
        .destructor = key.destructor,
        .value = tls_values_[value_index].value,
    };
    tls_values_[value_index] = {};
    return call;
}

void A32PthreadSyncService::clear_tls_values_for_thread(
    std::uint32_t thread_id) noexcept {
    if (thread_id == 0U) return;
    for (auto& value : tls_values_) {
        if (value.active && value.thread_id == thread_id) {
            value = {};
        }
    }
}

std::size_t A32PthreadSyncService::find_mutex(
    std::uint32_t address) const noexcept {
    for (std::size_t index = 0; index < mutexes_.size(); ++index) {
        if (mutexes_[index].address == address) return index;
    }
    return mutexes_.size();
}

std::size_t A32PthreadSyncService::ensure_mutex(
    std::uint32_t address) noexcept {
    const std::size_t existing = find_mutex(address);
    if (existing < mutexes_.size()) return existing;
    for (std::size_t index = 0; index < mutexes_.size(); ++index) {
        if (mutexes_[index].address == 0U) {
            mutexes_[index] = A32PthreadMutexState{.address = address};
            return index;
        }
    }
    return mutexes_.size();
}

std::size_t A32PthreadSyncService::find_semaphore(
    std::uint32_t address) const noexcept {
    for (std::size_t index = 0; index < semaphores_.size(); ++index) {
        if (semaphores_[index].address == address) return index;
    }
    return semaphores_.size();
}

std::size_t A32PthreadSyncService::allocate_semaphore(
    std::uint32_t address,
    std::uint32_t value) noexcept {
    if (find_semaphore(address) < semaphores_.size()) {
        return semaphores_.size();
    }
    for (std::size_t index = 0; index < semaphores_.size(); ++index) {
        if (semaphores_[index].address == 0U) {
            semaphores_[index] = A32SemaphoreState{
                .address = address,
                .value = value,
            };
            return index;
        }
    }
    return semaphores_.size();
}

bool A32PthreadSyncService::has_waiter(
    A32PthreadWaitKind kind,
    std::uint32_t object_address) const noexcept {
    for (const auto& waiter : waiters_) {
        if (waiter.kind == kind &&
            waiter.object_address == object_address) {
            return true;
        }
    }
    return false;
}

bool A32PthreadSyncService::enqueue_waiter(
    A32PthreadWaitKind kind,
    std::uint32_t object_address) noexcept {
    for (const auto& waiter : waiters_) {
        if (waiter.kind != A32PthreadWaitKind::None &&
            waiter.thread_id == current_thread_id_.value()) {
            return false;
        }
    }

    for (auto& waiter : waiters_) {
        if (waiter.kind == A32PthreadWaitKind::None) {
            waiter = A32PthreadWaiter{
                .kind = kind,
                .object_address = object_address,
                .thread_id = current_thread_id_.value(),
                .sequence = next_sequence_,
                .ready = false,
            };
            if (next_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
                ++next_sequence_;
            }
            return true;
        }
    }
    return false;
}


bool A32PthreadSyncService::has_mutex_waiter(
    std::uint32_t mutex_address) const noexcept {
    for (const auto& waiter : waiters_) {
        if ((waiter.kind == A32PthreadWaitKind::Mutex &&
             waiter.object_address == mutex_address) ||
            (waiter.kind == A32PthreadWaitKind::ConditionMutex &&
             waiter.mutex_address == mutex_address)) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t>
A32PthreadSyncService::enqueue_condition_waiter(
    std::uint32_t condition_address,
    std::uint32_t mutex_address,
    std::optional<std::int64_t> deadline_ns) noexcept {
    for (const auto& waiter : waiters_) {
        if (waiter.kind != A32PthreadWaitKind::None &&
            waiter.thread_id == current_thread_id_.value()) {
            return std::nullopt;
        }
    }

    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        if (waiters_[index].kind != A32PthreadWaitKind::None) continue;
        waiters_[index] = A32PthreadWaiter{
            .kind = A32PthreadWaitKind::Condition,
            .object_address = condition_address,
            .mutex_address = mutex_address,
            .thread_id = current_thread_id_.value(),
            .deadline_ns = deadline_ns.value_or(0),
            .sequence = next_sequence_,
            .timed = deadline_ns.has_value(),
            .ready = false,
        };
        if (next_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
            ++next_sequence_;
        }
        return index;
    }
    return std::nullopt;
}

std::optional<std::size_t>
A32PthreadSyncService::oldest_mutex_waiter(
    std::uint32_t mutex_address) const noexcept {
    std::optional<std::size_t> best;
    std::uint64_t best_sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        const auto& waiter = waiters_[index];
        const bool matches =
            (waiter.kind == A32PthreadWaitKind::Mutex &&
             waiter.object_address == mutex_address) ||
            (waiter.kind == A32PthreadWaitKind::ConditionMutex &&
             waiter.mutex_address == mutex_address);
        if (matches && !waiter.ready && waiter.sequence < best_sequence) {
            best = index;
            best_sequence = waiter.sequence;
        }
    }
    return best;
}

std::optional<std::size_t>
A32PthreadSyncService::oldest_condition_waiter(
    std::uint32_t condition_address) const noexcept {
    return oldest_waiter(
        A32PthreadWaitKind::Condition,
        condition_address,
        false);
}

bool A32PthreadSyncService::release_mutex(
    std::uint32_t mutex_address,
    std::uint32_t owner_thread_id) noexcept {
    const std::size_t index = find_mutex(mutex_address);
    if (index >= mutexes_.size() ||
        mutexes_[index].owner_thread_id != owner_thread_id) {
        return false;
    }

    const auto waiter = oldest_mutex_waiter(mutex_address);
    if (waiter.has_value()) {
        mutexes_[index].owner_thread_id = waiters_[*waiter].thread_id;
        waiters_[*waiter].ready = true;
    } else {
        mutexes_[index].owner_thread_id = 0U;
    }
    return true;
}

bool A32PthreadSyncService::transfer_condition_waiter(
    std::size_t waiter_index,
    std::uint32_t result_value) noexcept {
    if (waiter_index >= waiters_.size()) return false;
    auto& waiter = waiters_[waiter_index];
    if (waiter.kind != A32PthreadWaitKind::Condition ||
        waiter.mutex_address == 0U) {
        return false;
    }

    const std::size_t mutex_index = find_mutex(waiter.mutex_address);
    if (mutex_index >= mutexes_.size()) return false;

    waiter.kind = A32PthreadWaitKind::ConditionMutex;
    waiter.result_value = result_value;
    waiter.timed = false;
    waiter.deadline_ns = 0;
    waiter.sequence = next_sequence_;
    waiter.ready = false;
    if (next_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
        ++next_sequence_;
    }

    if (mutexes_[mutex_index].owner_thread_id == 0U) {
        mutexes_[mutex_index].owner_thread_id = waiter.thread_id;
        waiter.ready = true;
    }
    return true;
}

bool A32PthreadSyncService::process_expired_condition_waiters(
    std::int64_t now_ns) noexcept {
    while (true) {
        std::optional<std::size_t> oldest;
        std::uint64_t sequence = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t index = 0; index < waiters_.size(); ++index) {
            const auto& waiter = waiters_[index];
            if (waiter.kind == A32PthreadWaitKind::Condition &&
                waiter.timed &&
                waiter.deadline_ns <= now_ns &&
                waiter.sequence < sequence) {
                oldest = index;
                sequence = waiter.sequence;
            }
        }
        if (!oldest.has_value()) return true;
        if (!transfer_condition_waiter(
                *oldest,
                static_cast<std::uint32_t>(kA32AndroidEtimedout))) {
            return false;
        }
    }
}

bool A32PthreadSyncService::poll_condition_timeouts() noexcept {
    bool has_timed_waiter = false;
    for (const auto& waiter : waiters_) {
        if (waiter.kind == A32PthreadWaitKind::Condition && waiter.timed) {
            has_timed_waiter = true;
            break;
        }
    }
    if (!has_timed_waiter) return true;
    if (clock_ == nullptr) return false;

    const auto now = clock_->now_ns(A32PthreadClockId::Realtime);
    if (!now.has_value()) return false;
    return process_expired_condition_waiters(*now);
}

std::optional<std::int64_t>
A32PthreadSyncService::next_condition_deadline_ns() const noexcept {
    std::optional<std::int64_t> best;
    for (const auto& waiter : waiters_) {
        if (waiter.kind != A32PthreadWaitKind::Condition || !waiter.timed) {
            continue;
        }
        if (!best.has_value() || waiter.deadline_ns < *best) {
            best = waiter.deadline_ns;
        }
    }
    return best;
}

std::optional<std::size_t> A32PthreadSyncService::oldest_waiter(
    A32PthreadWaitKind kind,
    std::uint32_t object_address,
    bool ready) const noexcept {
    std::optional<std::size_t> best;
    std::uint64_t best_sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        const auto& waiter = waiters_[index];
        if (waiter.kind == kind &&
            waiter.object_address == object_address &&
            waiter.ready == ready &&
            waiter.sequence < best_sequence) {
            best = index;
            best_sequence = waiter.sequence;
        }
    }
    return best;
}

std::optional<A32PthreadWake> A32PthreadSyncService::pop_ready() noexcept {
    std::optional<std::size_t> best;
    std::uint64_t best_sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        const auto& waiter = waiters_[index];
        if (waiter.kind != A32PthreadWaitKind::None &&
            waiter.ready &&
            waiter.sequence < best_sequence) {
            best = index;
            best_sequence = waiter.sequence;
        }
    }
    if (!best.has_value()) return std::nullopt;

    const A32PthreadWaiter waiter = waiters_[*best];
    waiters_[*best] = {};
    return A32PthreadWake{
        .kind = waiter.kind == A32PthreadWaitKind::ConditionMutex
            ? A32PthreadWaitKind::Condition
            : waiter.kind,
        .object_address = waiter.object_address,
        .mutex_address = waiter.mutex_address,
        .thread_id = waiter.thread_id,
        .result_value = waiter.result_value,
    };
}

runtime::A32HostServiceDisposition A32PthreadSyncService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_sync_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }

    const bool tls_service =
        svc_immediate == kA32PthreadKeyCreateSvcImmediate ||
        svc_immediate == kA32PthreadKeyDeleteSvcImmediate ||
        svc_immediate == kA32PthreadGetspecificSvcImmediate ||
        svc_immediate == kA32PthreadSetspecificSvcImmediate;
    if (tls_service) {
        if (!tls_configuration_valid()) {
            return A32HostServiceDisposition::Failed;
        }

        if (svc_immediate == kA32PthreadKeyCreateSvcImmediate) {
            const std::uint32_t key_address = regs[0];
            if (key_address == 0U) {
                return A32HostServiceDisposition::Failed;
            }
            const std::size_t index = allocate_key(regs[1]);
            if (index >= keys_.size()) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEagain);
                return A32HostServiceDisposition::Handled;
            }
            if (!write_u32_le(memory, key_address, keys_[index].key)) {
                keys_[index] = {};
                return A32HostServiceDisposition::Failed;
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        const std::uint32_t key = regs[0];
        const std::size_t key_index = find_key(key);
        if (svc_immediate == kA32PthreadGetspecificSvcImmediate) {
            if (key_index >= keys_.size()) {
                regs[0] = 0U;
                return A32HostServiceDisposition::Handled;
            }
            const std::size_t value_index =
                find_tls_value(key, current_thread_id_.value());
            regs[0] = value_index < tls_values_.size()
                ? tls_values_[value_index].value
                : 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (key_index >= keys_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadKeyDeleteSvcImmediate) {
            for (auto& value : tls_values_) {
                if (value.active && value.key == key) {
                    value = {};
                }
            }
            keys_[key_index] = {};
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        const std::uint32_t value = regs[1];
        const std::size_t existing =
            find_tls_value(key, current_thread_id_.value());
        if (value == 0U) {
            if (existing < tls_values_.size()) {
                tls_values_[existing] = {};
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (existing < tls_values_.size()) {
            tls_values_[existing].value = value;
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (allocate_tls_value(
                key, current_thread_id_.value(), value) >= tls_values_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEnomem);
            return A32HostServiceDisposition::Handled;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }


    const bool condition_service =
        svc_immediate == kA32PthreadCondInitSvcImmediate ||
        svc_immediate == kA32PthreadCondDestroySvcImmediate ||
        svc_immediate == kA32PthreadCondWaitSvcImmediate ||
        svc_immediate == kA32PthreadCondTimedwaitSvcImmediate ||
        svc_immediate == kA32PthreadCondSignalSvcImmediate ||
        svc_immediate == kA32PthreadCondBroadcastSvcImmediate;
    if (condition_service) {
        if (!sync_configuration_valid() || regs[0] == 0U) {
            return A32HostServiceDisposition::Failed;
        }

        const std::uint32_t condition_address = regs[0];

        if (svc_immediate == kA32PthreadCondInitSvcImmediate) {
            if (regs[1] != 0U) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            } else {
                regs[0] = 0U;
            }
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadCondDestroySvcImmediate) {
            // Bionic's cond_destroy does not reject active waiters. Using a
            // condition concurrently with destroy is application UB; preserve
            // existing waiter records rather than inventing host-side teardown.
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadCondSignalSvcImmediate ||
            svc_immediate == kA32PthreadCondBroadcastSvcImmediate) {
            if (!poll_condition_timeouts()) {
                return A32HostServiceDisposition::Failed;
            }

            const bool broadcast =
                svc_immediate == kA32PthreadCondBroadcastSvcImmediate;
            do {
                const auto waiter =
                    oldest_condition_waiter(condition_address);
                if (!waiter.has_value()) break;
                if (!transfer_condition_waiter(*waiter, 0U)) {
                    return A32HostServiceDisposition::Failed;
                }
                if (!broadcast) break;
            } while (true);

            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        const std::uint32_t mutex_address = regs[1];
        const std::size_t mutex_index = find_mutex(mutex_address);
        if (mutex_address == 0U ||
            mutex_index >= mutexes_.size() ||
            mutexes_[mutex_index].owner_thread_id !=
                current_thread_id_.value()) {
            return A32HostServiceDisposition::Failed;
        }

        std::optional<std::int64_t> deadline;
        std::optional<std::int64_t> now;
        if (svc_immediate == kA32PthreadCondTimedwaitSvcImmediate &&
            regs[2] != 0U) {
            std::array<std::uint8_t, 8> bytes{};
            if (!memory.read(regs[2], bytes)) {
                return A32HostServiceDisposition::Failed;
            }
            const std::uint32_t sec_bits =
                static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[3]) << 24U);
            const std::uint32_t nsec_bits =
                static_cast<std::uint32_t>(bytes[4]) |
                (static_cast<std::uint32_t>(bytes[5]) << 8U) |
                (static_cast<std::uint32_t>(bytes[6]) << 16U) |
                (static_cast<std::uint32_t>(bytes[7]) << 24U);
            const std::int32_t seconds =
                static_cast<std::int32_t>(sec_bits);
            const std::int32_t nanoseconds =
                static_cast<std::int32_t>(nsec_bits);
            if (nanoseconds < 0 || nanoseconds >= 1000000000) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
                return A32HostServiceDisposition::Handled;
            }
            if (seconds < 0) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEtimedout);
                return A32HostServiceDisposition::Handled;
            }
            if (clock_ == nullptr) {
                return A32HostServiceDisposition::Failed;
            }
            now = clock_->now_ns(A32PthreadClockId::Realtime);
            if (!now.has_value()) {
                return A32HostServiceDisposition::Failed;
            }
            deadline =
                static_cast<std::int64_t>(seconds) * 1000000000LL +
                static_cast<std::int64_t>(nanoseconds);
        }

        const auto waiter = enqueue_condition_waiter(
            condition_address,
            mutex_address,
            deadline);
        if (!waiter.has_value()) {
            return A32HostServiceDisposition::Failed;
        }
        if (!release_mutex(
                mutex_address,
                current_thread_id_.value())) {
            waiters_[*waiter] = {};
            return A32HostServiceDisposition::Failed;
        }

        if (deadline.has_value() && *deadline <= *now) {
            if (!transfer_condition_waiter(
                    *waiter,
                    static_cast<std::uint32_t>(kA32AndroidEtimedout))) {
                return A32HostServiceDisposition::Failed;
            }
            if (waiters_[*waiter].ready) {
                const std::uint32_t result_value =
                    waiters_[*waiter].result_value;
                waiters_[*waiter] = {};
                regs[0] = result_value;
                return A32HostServiceDisposition::Handled;
            }
        }

        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    if (!sync_configuration_valid() || regs[0] == 0U) {
        return A32HostServiceDisposition::Failed;
    }

    const std::uint32_t object_address = regs[0];

    if (svc_immediate == kA32PthreadMutexInitSvcImmediate) {
        if (regs[1] != 0U || find_mutex(object_address) < mutexes_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        if (ensure_mutex(object_address) >= mutexes_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadMutexDestroySvcImmediate) {
        const std::size_t index = find_mutex(object_address);
        if (index >= mutexes_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        if (mutexes_[index].owner_thread_id != 0U ||
            has_mutex_waiter(object_address)) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEbusy);
            return A32HostServiceDisposition::Handled;
        }
        mutexes_[index] = {};
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadMutexLockSvcImmediate ||
        svc_immediate == kA32PthreadMutexTrylockSvcImmediate) {
        const std::size_t index = ensure_mutex(object_address);
        if (index >= mutexes_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        if (mutexes_[index].owner_thread_id == 0U) {
            mutexes_[index].owner_thread_id = current_thread_id_.value();
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (svc_immediate == kA32PthreadMutexTrylockSvcImmediate) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEbusy);
            return A32HostServiceDisposition::Handled;
        }
        if (!enqueue_waiter(A32PthreadWaitKind::Mutex, object_address)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    if (svc_immediate == kA32PthreadMutexUnlockSvcImmediate) {
        const std::size_t index = find_mutex(object_address);
        if (index >= mutexes_.size() ||
            mutexes_[index].owner_thread_id != current_thread_id_.value()) {
            return A32HostServiceDisposition::Failed;
        }
        if (!release_mutex(
                object_address,
                current_thread_id_.value())) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SemInitSvcImmediate) {
        const std::uint32_t pshared = regs[1];
        const std::uint32_t value = regs[2];
        if (pshared != 0U || value > kA32AndroidSemValueMax ||
            allocate_semaphore(object_address, value) >= semaphores_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SemDestroySvcImmediate) {
        const std::size_t index = find_semaphore(object_address);
        if (index >= semaphores_.size() ||
            has_waiter(A32PthreadWaitKind::Semaphore, object_address)) {
            return A32HostServiceDisposition::Failed;
        }
        semaphores_[index] = {};
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SemWaitSvcImmediate) {
        const std::size_t index = find_semaphore(object_address);
        if (index >= semaphores_.size()) {
            return A32HostServiceDisposition::Failed;
        }
        if (semaphores_[index].value != 0U) {
            --semaphores_[index].value;
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (!enqueue_waiter(A32PthreadWaitKind::Semaphore, object_address)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    const std::size_t index = find_semaphore(object_address);
    if (index >= semaphores_.size()) {
        return A32HostServiceDisposition::Failed;
    }
    const auto waiter = oldest_waiter(
        A32PthreadWaitKind::Semaphore, object_address, false);
    if (waiter.has_value()) {
        waiters_[*waiter].ready = true;
    } else {
        if (semaphores_[index].value == kA32AndroidSemValueMax) {
            return A32HostServiceDisposition::Failed;
        }
        ++semaphores_[index].value;
    }
    regs[0] = 0U;
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
