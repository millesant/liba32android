#include "compat/a32_signal.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

[[nodiscard]] bool is_signal_svc(std::uint32_t svc) noexcept {
    switch (svc) {
    case kA32RaiseSvcImmediate:
    case kA32SigactionSvcImmediate:
    case kA32SigpendingSvcImmediate:
    case kA32PthreadSigmaskSvcImmediate:
    case kA32SigwaitSvcImmediate:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool valid_public_signal(
    std::uint32_t signal) noexcept {
    return signal >= 1U && signal <= kA32SignalMaxPublic;
}

[[nodiscard]] constexpr std::uint32_t unblockable_mask() noexcept {
    return a32_signal_bit(kA32SigKill) | a32_signal_bit(kA32SigStop);
}

[[nodiscard]] constexpr std::uint32_t sanitize_mask(
    std::uint32_t mask) noexcept {
    return mask & kA32SignalSupportedMask & ~unblockable_mask();
}

[[nodiscard]] bool read_u32_le(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    if (address == 0U ||
        address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] bool write_u32_le(
    memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t value) {
    if (address == 0U ||
        address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    }};
    return memory.write(address, bytes);
}

[[nodiscard]] bool read_action(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    A32SignalActionState& action) {
    std::array<std::uint32_t, 4> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint64_t word_address =
            static_cast<std::uint64_t>(address) + index * 4U;
        if (word_address > std::numeric_limits<std::uint32_t>::max() ||
            !read_u32_le(
                memory,
                static_cast<std::uint32_t>(word_address),
                words[index])) {
            return false;
        }
    }
    action = A32SignalActionState{
        .handler = words[0],
        .mask = words[1],
        .flags = words[2],
        .restorer = words[3],
    };
    return true;
}

[[nodiscard]] bool write_action(
    memory::GuestMemory& memory,
    std::uint32_t address,
    const A32SignalActionState& action) {
    const std::array<std::uint32_t, 4> words{{
        action.handler,
        action.mask,
        action.flags,
        action.restorer,
    }};
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint64_t word_address =
            static_cast<std::uint64_t>(address) + index * 4U;
        if (word_address > std::numeric_limits<std::uint32_t>::max() ||
            !write_u32_le(
                memory,
                static_cast<std::uint32_t>(word_address),
                words[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t lowest_signal(std::uint32_t mask) noexcept {
    if (mask == 0U) return 0U;
    return static_cast<std::uint32_t>(std::countr_zero(mask)) + 1U;
}

[[nodiscard]] constexpr bool default_ignored(
    std::uint32_t signal) noexcept {
    return signal == kA32SigChld ||
           signal == kA32SigUrg ||
           signal == kA32SigWinch;
}

[[nodiscard]] constexpr bool default_stopped(
    std::uint32_t signal) noexcept {
    return signal == kA32SigStop ||
           signal == kA32SigTstp ||
           signal == kA32SigTtin ||
           signal == kA32SigTtou;
}

}  // namespace

A32SignalService::A32SignalService(
    std::span<A32PthreadThreadState> threads,
    std::span<A32SignalActionState> actions,
    std::span<A32SignalWaiter> waiters,
    A32LibcErrnoSink& errno_sink) noexcept
    : threads_(threads),
      actions_(actions),
      waiters_(waiters),
      errno_sink_(errno_sink) {
    for (auto& action : actions_) action = {};
    for (auto& waiter : waiters_) waiter = {};
}

bool A32SignalService::configuration_valid() const noexcept {
    if (!current_thread_id_.valid() ||
        threads_.empty() ||
        actions_.size() < kA32SignalSetBits) {
        return false;
    }
    const std::size_t current = find_thread(current_thread_id_.value());
    return current < threads_.size() &&
           threads_[current].phase == A32PthreadThreadPhase::Running;
}

std::size_t A32SignalService::find_thread(
    std::uint32_t thread_id) const noexcept {
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        if (threads_[index].phase != A32PthreadThreadPhase::Free &&
            threads_[index].pthread_id == thread_id) {
            return index;
        }
    }
    return threads_.size();
}

std::optional<std::size_t> A32SignalService::oldest_waiter(
    std::uint32_t signal_bit) const noexcept {
    std::optional<std::size_t> best;
    std::uint64_t sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        const auto& waiter = waiters_[index];
        if (!waiter.active || waiter.ready ||
            (waiter.mask & signal_bit) == 0U) {
            continue;
        }
        if (waiter.sequence < sequence) {
            best = index;
            sequence = waiter.sequence;
        }
    }
    return best;
}

bool A32SignalService::disposition_ignored(
    std::uint32_t signal) const noexcept {
    if (!valid_public_signal(signal)) return false;
    const auto& action = actions_[signal - 1U];
    return action.handler == kA32SignalIgnore ||
           (action.handler == kA32SignalDefault &&
            default_ignored(signal));
}

A32SignalBoundaryError A32SignalService::classify_unblocked(
    std::uint32_t signal) const noexcept {
    const auto& action = actions_[signal - 1U];
    if (action.handler == kA32SignalIgnore ||
        (action.handler == kA32SignalDefault &&
         default_ignored(signal))) {
        return A32SignalBoundaryError::None;
    }
    if (action.handler != kA32SignalDefault) {
        return A32SignalBoundaryError::CustomHandlerUnsupported;
    }
    if (default_stopped(signal)) {
        return A32SignalBoundaryError::DefaultStopUnsupported;
    }
    if (signal == kA32SigCont) {
        return A32SignalBoundaryError::DefaultContinueUnsupported;
    }
    return A32SignalBoundaryError::DefaultFatal;
}

void A32SignalService::clear_pending_signal(
    std::uint32_t signal) noexcept {
    const std::uint32_t bit = a32_signal_bit(signal);
    process_pending_ &= ~bit;
    for (auto& thread : threads_) {
        thread.signal_pending &= ~bit;
    }
}

bool A32SignalService::resolve_unblocked_pending(
    A32PthreadThreadState& thread) noexcept {
    for (;;) {
        const std::uint32_t pending =
            (thread.signal_pending | process_pending_) &
            ~thread.signal_mask &
            kA32SignalSupportedMask;
        if (pending == 0U) return true;

        const std::uint32_t signal = lowest_signal(pending);
        if (disposition_ignored(signal)) {
            clear_pending_signal(signal);
            continue;
        }

        last_boundary_error_ = classify_unblocked(signal);
        last_boundary_signal_ = signal;
        return false;
    }
}

std::optional<A32SignalWake> A32SignalService::pop_ready() noexcept {
    std::optional<std::size_t> best;
    std::uint64_t sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < waiters_.size(); ++index) {
        const auto& waiter = waiters_[index];
        if (!waiter.active || !waiter.ready) continue;
        if (waiter.sequence < sequence) {
            best = index;
            sequence = waiter.sequence;
        }
    }
    if (!best.has_value()) return std::nullopt;

    const A32SignalWake wake{
        .thread_id = waiters_[*best].thread_id,
        .signal = waiters_[*best].delivered_signal,
        .result_value = waiters_[*best].result_value,
    };
    waiters_[*best] = {};
    return wake;
}

bool A32SignalService::queue_process_signal(
    memory::GuestMemory& memory,
    std::uint32_t signal) noexcept {
    if (!valid_public_signal(signal)) return false;
    const std::uint32_t bit = a32_signal_bit(signal);
    if (disposition_ignored(signal)) {
        clear_pending_signal(signal);
        return true;
    }

    const auto waiter = oldest_waiter(bit);
    if (waiter.has_value()) {
        auto& state = waiters_[*waiter];
        state.delivered_signal = signal;
        state.result_value =
            write_u32_le(memory, state.signal_out_address, signal)
                ? 0U
                : static_cast<std::uint32_t>(kA32AndroidEfault);
        state.ready = true;
        return true;
    }

    process_pending_ |= bit;
    return true;
}

runtime::A32HostServiceDisposition A32SignalService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_signal_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid()) {
        return A32HostServiceDisposition::Failed;
    }

    last_boundary_error_ = A32SignalBoundaryError::None;
    last_boundary_signal_ = 0U;

    const std::size_t current_index =
        find_thread(current_thread_id_.value());
    auto& current = threads_[current_index];

    const auto libc_error = [&](std::int32_t error) {
        if (!errno_sink_.set_errno(memory, error)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = std::numeric_limits<std::uint32_t>::max();
        return A32HostServiceDisposition::Handled;
    };

    if (svc_immediate == kA32SigactionSvcImmediate) {
        const std::uint32_t signal = regs[0];
        if (!valid_public_signal(signal) ||
            signal == kA32SigKill ||
            signal == kA32SigStop) {
            return libc_error(kA32AndroidEinval);
        }

        A32SignalActionState replacement{};
        const bool has_replacement = regs[1] != 0U;
        if (has_replacement) {
            if (!read_action(memory, regs[1], replacement)) {
                return libc_error(kA32AndroidEfault);
            }
            replacement.mask = sanitize_mask(replacement.mask);
        }

        const A32SignalActionState previous = actions_[signal - 1U];
        if (regs[2] != 0U &&
            !write_action(memory, regs[2], previous)) {
            return libc_error(kA32AndroidEfault);
        }

        if (has_replacement) {
            actions_[signal - 1U] = replacement;
            if (disposition_ignored(signal)) {
                clear_pending_signal(signal);
            }
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadSigmaskSvcImmediate) {
        const std::uint32_t set_address = regs[1];
        const std::uint32_t old_address = regs[2];

        if (old_address != 0U &&
            !write_u32_le(memory, old_address, current.signal_mask)) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEfault);
            return A32HostServiceDisposition::Handled;
        }
        if (set_address == 0U) {
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }
        if (regs[0] != kA32SigBlock &&
            regs[0] != kA32SigUnblock &&
            regs[0] != kA32SigSetmask) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }

        std::uint32_t requested{};
        if (!read_u32_le(memory, set_address, requested)) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEfault);
            return A32HostServiceDisposition::Handled;
        }
        requested = sanitize_mask(requested);

        if (regs[0] == kA32SigBlock) {
            current.signal_mask |= requested;
        } else if (regs[0] == kA32SigUnblock) {
            current.signal_mask &= ~requested;
        } else {
            current.signal_mask = requested;
        }
        current.signal_mask = sanitize_mask(current.signal_mask);

        if (!resolve_unblocked_pending(current)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SigpendingSvcImmediate) {
        if (!write_u32_le(
                memory,
                regs[0],
                (current.signal_pending | process_pending_) &
                    kA32SignalSupportedMask)) {
            return libc_error(kA32AndroidEfault);
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SigwaitSvcImmediate) {
        std::uint32_t requested{};
        std::uint32_t existing_out{};
        if (!read_u32_le(memory, regs[0], requested) ||
            !read_u32_le(memory, regs[1], existing_out) ||
            !write_u32_le(memory, regs[1], existing_out)) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEfault);
            return A32HostServiceDisposition::Handled;
        }
        requested &= kA32SignalSupportedMask;

        const std::uint32_t matching =
            (current.signal_pending | process_pending_) & requested;
        if (matching != 0U) {
            const std::uint32_t signal = lowest_signal(matching);
            const std::uint32_t bit = a32_signal_bit(signal);
            if ((current.signal_pending & bit) != 0U) {
                current.signal_pending &= ~bit;
            } else {
                process_pending_ &= ~bit;
            }
            if (!write_u32_le(memory, regs[1], signal)) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEfault);
                return A32HostServiceDisposition::Handled;
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        std::optional<std::size_t> slot;
        for (std::size_t index = 0; index < waiters_.size(); ++index) {
            if (!waiters_[index].active) {
                slot = index;
                break;
            }
        }
        if (!slot.has_value()) {
            last_boundary_error_ =
                A32SignalBoundaryError::WaiterCapacityExceeded;
            return A32HostServiceDisposition::Failed;
        }

        waiters_[*slot] = A32SignalWaiter{
            .thread_id = current_thread_id_.value(),
            .mask = requested,
            .signal_out_address = regs[1],
            .sequence = next_sequence_,
            .active = true,
        };
        if (next_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
            ++next_sequence_;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    if (svc_immediate != kA32RaiseSvcImmediate) {
        return A32HostServiceDisposition::Unhandled;
    }

    const std::uint32_t signal = regs[0];
    if (!valid_public_signal(signal)) {
        return libc_error(kA32AndroidEinval);
    }
    if (disposition_ignored(signal)) {
        clear_pending_signal(signal);
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    const std::uint32_t bit = a32_signal_bit(signal);
    if ((current.signal_mask & bit) != 0U) {
        current.signal_pending |= bit;
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    last_boundary_error_ = classify_unblocked(signal);
    last_boundary_signal_ = signal;
    return A32HostServiceDisposition::Failed;
}

const char* to_string(A32SignalBoundaryError error) noexcept {
    switch (error) {
    case A32SignalBoundaryError::None: return "none";
    case A32SignalBoundaryError::DefaultFatal: return "default_fatal";
    case A32SignalBoundaryError::DefaultStopUnsupported:
        return "default_stop_unsupported";
    case A32SignalBoundaryError::DefaultContinueUnsupported:
        return "default_continue_unsupported";
    case A32SignalBoundaryError::CustomHandlerUnsupported:
        return "custom_handler_unsupported";
    case A32SignalBoundaryError::WaiterCapacityExceeded:
        return "waiter_capacity_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::compat
