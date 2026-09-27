#include "compat/a32_pthread_sync.h"

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
        return true;
    default:
        return false;
    }
}

}  // namespace

A32PthreadSyncService::A32PthreadSyncService(
    std::span<A32PthreadMutexState> mutexes,
    std::span<A32SemaphoreState> semaphores,
    std::span<A32PthreadWaiter> waiters) noexcept
    : mutexes_(mutexes),
      semaphores_(semaphores),
      waiters_(waiters) {
    for (auto& mutex : mutexes_) mutex = {};
    for (auto& semaphore : semaphores_) semaphore = {};
    for (auto& waiter : waiters_) waiter = {};
}

bool A32PthreadSyncService::configuration_valid() const noexcept {
    return current_thread_id_ != 0U &&
           !mutexes_.empty() &&
           !semaphores_.empty() &&
           !waiters_.empty();
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
            waiter.thread_id == current_thread_id_) {
            return false;
        }
    }

    for (auto& waiter : waiters_) {
        if (waiter.kind == A32PthreadWaitKind::None) {
            waiter = A32PthreadWaiter{
                .kind = kind,
                .object_address = object_address,
                .thread_id = current_thread_id_,
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
        .kind = waiter.kind,
        .object_address = waiter.object_address,
        .thread_id = waiter.thread_id,
    };
}

runtime::A32HostServiceDisposition A32PthreadSyncService::handle(
    memory::GuestMemory&,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_sync_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid() || regs[0] == 0U) {
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
            has_waiter(A32PthreadWaitKind::Mutex, object_address)) {
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
            mutexes_[index].owner_thread_id = current_thread_id_;
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
            mutexes_[index].owner_thread_id != current_thread_id_) {
            return A32HostServiceDisposition::Failed;
        }
        const auto waiter = oldest_waiter(
            A32PthreadWaitKind::Mutex, object_address, false);
        if (waiter.has_value()) {
            mutexes_[index].owner_thread_id = waiters_[*waiter].thread_id;
            waiters_[*waiter].ready = true;
        } else {
            mutexes_[index].owner_thread_id = 0U;
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
