#include "compat/a32_scheduler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

[[nodiscard]] bool is_scheduler_svc(std::uint32_t svc) noexcept {
    switch (svc) {
    case kA32SetprioritySvcImmediate:
    case kA32SchedGetPriorityMaxSvcImmediate:
    case kA32SchedGetPriorityMinSvcImmediate:
    case kA32SchedGetaffinitySvcImmediate:
    case kA32SchedSetschedulerSvcImmediate:
    case kA32SchedYieldSvcImmediate:
        return true;
    default:
        return false;
    }
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

[[nodiscard]] bool known_policy(std::int32_t policy) noexcept {
    switch (policy) {
    case kA32SchedOther:
    case kA32SchedFifo:
    case kA32SchedRr:
    case kA32SchedBatch:
    case kA32SchedIdle:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool normal_policy(std::int32_t policy) noexcept {
    return policy == kA32SchedOther ||
           policy == kA32SchedBatch ||
           policy == kA32SchedIdle;
}

[[nodiscard]] std::int32_t policy_priority_limit(
    std::int32_t policy,
    bool maximum,
    bool& valid) noexcept {
    valid = true;
    if (normal_policy(policy)) return 0;
    if (policy == kA32SchedFifo || policy == kA32SchedRr) {
        return maximum
            ? kA32SchedPriorityMaxRealtime
            : kA32SchedPriorityMinRealtime;
    }
    valid = false;
    return -1;
}

}  // namespace

bool A32SchedulerService::configuration_valid() const noexcept {
    if (!current_thread_id_.valid() ||
        threads_.empty() ||
        options_.logical_cpu_count == 0U ||
        options_.logical_cpu_count > 32U) {
        return false;
    }
    const std::size_t current = find_thread(current_thread_id_.value());
    return current < threads_.size() &&
           threads_[current].phase == A32PthreadThreadPhase::Running;
}

std::size_t A32SchedulerService::find_thread(
    std::uint32_t thread_id) const noexcept {
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        if (threads_[index].phase != A32PthreadThreadPhase::Free &&
            threads_[index].pthread_id == thread_id) {
            return index;
        }
    }
    return threads_.size();
}

runtime::A32HostServiceDisposition A32SchedulerService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_scheduler_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid()) {
        return A32HostServiceDisposition::Failed;
    }

    const std::size_t current_index = find_thread(current_thread_id_.value());
    auto& current = threads_[current_index];

    const auto libc_error = [&](std::int32_t error) {
        if (!errno_sink_.set_errno(memory, error)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = std::numeric_limits<std::uint32_t>::max();
        return A32HostServiceDisposition::Handled;
    };

    if (svc_immediate == kA32SchedGetPriorityMaxSvcImmediate ||
        svc_immediate == kA32SchedGetPriorityMinSvcImmediate) {
        const std::int32_t policy = std::bit_cast<std::int32_t>(regs[0]);
        bool valid = false;
        const std::int32_t value = policy_priority_limit(
            policy,
            svc_immediate == kA32SchedGetPriorityMaxSvcImmediate,
            valid);
        if (!valid) return libc_error(kA32AndroidEinval);
        regs[0] = std::bit_cast<std::uint32_t>(value);
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SchedGetaffinitySvcImmediate) {
        const std::int32_t pid = std::bit_cast<std::int32_t>(regs[0]);
        if (pid < 0) return libc_error(kA32AndroidEinval);
        if (pid != 0) return libc_error(kA32AndroidEsrch);
        if (regs[1] != kA32CpuSetBytes) {
            return libc_error(kA32AndroidEinval);
        }

        const std::uint32_t mask =
            options_.logical_cpu_count == 32U
                ? std::numeric_limits<std::uint32_t>::max()
                : ((1U << options_.logical_cpu_count) - 1U);
        if (!write_u32_le(memory, regs[2], mask)) {
            return libc_error(kA32AndroidEfault);
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SchedSetschedulerSvcImmediate) {
        const std::int32_t pid = std::bit_cast<std::int32_t>(regs[0]);
        if (pid < 0) return libc_error(kA32AndroidEinval);
        if (pid != 0) return libc_error(kA32AndroidEsrch);
        if (regs[2] == 0U) return libc_error(kA32AndroidEinval);

        std::uint32_t priority_word{};
        if (!read_u32_le(memory, regs[2], priority_word)) {
            return libc_error(kA32AndroidEfault);
        }
        const std::int32_t policy = std::bit_cast<std::int32_t>(regs[1]);
        const std::int32_t priority =
            std::bit_cast<std::int32_t>(priority_word);
        if (!known_policy(policy)) {
            return libc_error(kA32AndroidEinval);
        }

        if (normal_policy(policy)) {
            if (priority != 0) return libc_error(kA32AndroidEinval);
            if (policy != kA32SchedOther) {
                return libc_error(kA32AndroidEperm);
            }
            current.sched_policy = policy;
            current.sched_priority = 0;
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (priority < kA32SchedPriorityMinRealtime ||
            priority > kA32SchedPriorityMaxRealtime) {
            return libc_error(kA32AndroidEinval);
        }
        return libc_error(kA32AndroidEperm);
    }

    if (svc_immediate == kA32SetprioritySvcImmediate) {
        const std::int32_t which = std::bit_cast<std::int32_t>(regs[0]);
        if (which != kA32PrioProcess) {
            return libc_error(kA32AndroidEinval);
        }
        if (regs[1] != 0U) {
            return libc_error(kA32AndroidEsrch);
        }

        const std::int32_t requested = std::clamp(
            std::bit_cast<std::int32_t>(regs[2]),
            kA32NiceMin,
            kA32NiceMax);
        if (requested < current.nice_value) {
            return libc_error(kA32AndroidEacces);
        }
        current.nice_value = requested;
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32SchedYieldSvcImmediate) {
        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    return A32HostServiceDisposition::Unhandled;
}

}  // namespace liba32android::compat
