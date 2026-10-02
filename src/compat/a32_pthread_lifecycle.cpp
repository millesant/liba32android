#include "compat/a32_pthread_lifecycle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

using runtime::A32HostServiceDisposition;

[[nodiscard]] bool is_lifecycle_svc(std::uint32_t svc) noexcept {
    switch (svc) {
    case kA32PthreadAttrInitSvcImmediate:
    case kA32PthreadAttrDestroySvcImmediate:
    case kA32PthreadAttrGetdetachstateSvcImmediate:
    case kA32PthreadAttrSetdetachstateSvcImmediate:
    case kA32PthreadAttrGetstacksizeSvcImmediate:
    case kA32PthreadAttrSetstacksizeSvcImmediate:
    case kA32PthreadCreateSvcImmediate:
    case kA32PthreadSelfSvcImmediate:
    case kA32PthreadEqualSvcImmediate:
    case kA32PthreadExitSvcImmediate:
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

[[nodiscard]] constexpr bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::optional<std::uint64_t> align_up(
    std::uint64_t value,
    std::uint64_t alignment) noexcept {
    if (alignment == 0U) return std::nullopt;
    const std::uint64_t mask = alignment - 1U;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
        return std::nullopt;
    }
    return (value + mask) & ~mask;
}

[[nodiscard]] constexpr bool ranges_overlap(
    std::uint64_t lhs_begin,
    std::uint64_t lhs_end,
    std::uint64_t rhs_begin,
    std::uint64_t rhs_end) noexcept {
    return lhs_begin < rhs_end && rhs_begin < lhs_end;
}

}  // namespace

A32PthreadLifecycleService::A32PthreadLifecycleService(
    A32PthreadLifecycleOptions options,
    std::span<A32PthreadAttrState> attrs,
    std::span<A32PthreadThreadState> threads) noexcept
    : options_(options),
      attrs_(attrs),
      threads_(threads),
      next_thread_id_(options.first_thread_id) {
    for (auto& attr : attrs_) attr = {};
    for (auto& thread : threads_) thread = {};
}

bool A32PthreadLifecycleService::configuration_valid() const noexcept {
    if (attrs_.empty() || threads_.empty() ||
        !is_power_of_two(options_.page_size) ||
        options_.stack_arena_base == 0U ||
        options_.stack_arena_size == 0U ||
        (options_.stack_arena_base & (options_.page_size - 1U)) != 0U ||
        (options_.stack_arena_size & (options_.page_size - 1U)) != 0U ||
        options_.default_stack_size <
            static_cast<std::uint64_t>(options_.page_size) * 2U ||
        options_.exit_trampoline == 0U ||
        options_.thread_instruction_budget == 0U ||
        options_.first_thread_id == 0U) {
        return false;
    }

    const std::uint64_t arena_end =
        static_cast<std::uint64_t>(options_.stack_arena_base) +
        options_.stack_arena_size;
    return arena_end <= std::numeric_limits<std::uint32_t>::max();
}

std::size_t A32PthreadLifecycleService::find_attr(
    std::uint32_t address) const noexcept {
    for (std::size_t index = 0; index < attrs_.size(); ++index) {
        if (attrs_[index].active && attrs_[index].address == address) {
            return index;
        }
    }
    return attrs_.size();
}

std::size_t A32PthreadLifecycleService::ensure_attr(
    std::uint32_t address) noexcept {
    const std::size_t existing = find_attr(address);
    if (existing < attrs_.size()) return existing;
    for (std::size_t index = 0; index < attrs_.size(); ++index) {
        if (!attrs_[index].active) return index;
    }
    return attrs_.size();
}

std::size_t A32PthreadLifecycleService::find_thread(
    std::uint32_t pthread_id) const noexcept {
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        if (threads_[index].phase != A32PthreadThreadPhase::Free &&
            threads_[index].pthread_id == pthread_id) {
            return index;
        }
    }
    return threads_.size();
}

std::size_t A32PthreadLifecycleService::free_thread_slot() const noexcept {
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        if (threads_[index].phase == A32PthreadThreadPhase::Free) {
            return index;
        }
    }
    return threads_.size();
}

std::optional<std::uint32_t>
A32PthreadLifecycleService::choose_thread_id() const noexcept {
    std::uint32_t candidate = next_thread_id_;
    for (std::size_t attempt = 0; attempt <= threads_.size(); ++attempt) {
        if (candidate == 0U) return std::nullopt;
        if (find_thread(candidate) >= threads_.size()) {
            return candidate;
        }
        if (candidate == std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        ++candidate;
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
A32PthreadLifecycleService::choose_stack_base(
    std::uint32_t requested_size,
    std::uint32_t& allocated_size) const noexcept {
    allocated_size = 0U;
    const auto aligned_size =
        align_up(requested_size, options_.page_size);
    if (!aligned_size.has_value() ||
        *aligned_size == 0U ||
        *aligned_size > options_.stack_arena_size ||
        *aligned_size > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    const std::uint64_t arena_begin = options_.stack_arena_base;
    const std::uint64_t arena_end =
        arena_begin + options_.stack_arena_size;
    std::uint64_t candidate = arena_begin;

    for (std::size_t attempt = 0; attempt <= threads_.size(); ++attempt) {
        if (candidate + *aligned_size > arena_end) {
            return std::nullopt;
        }

        bool overlap = false;
        std::uint64_t move_to = candidate;
        for (const auto& thread : threads_) {
            if (thread.phase == A32PthreadThreadPhase::Free ||
                !thread.owns_stack ||
                thread.stack_size == 0U) {
                continue;
            }
            const std::uint64_t thread_begin = thread.stack_base;
            const std::uint64_t thread_end =
                thread_begin + thread.stack_size;
            if (ranges_overlap(
                    candidate,
                    candidate + *aligned_size,
                    thread_begin,
                    thread_end)) {
                overlap = true;
                move_to = std::max(move_to, thread_end);
            }
        }

        if (!overlap) {
            allocated_size = static_cast<std::uint32_t>(*aligned_size);
            return static_cast<std::uint32_t>(candidate);
        }

        const auto next = align_up(move_to, options_.page_size);
        if (!next.has_value() || *next <= candidate) {
            return std::nullopt;
        }
        candidate = *next;
    }

    return std::nullopt;
}

std::optional<runtime::A32LogicalExecutionContext>
A32PthreadLifecycleService::make_thread_context(
    std::uint32_t pthread_id,
    std::uint32_t start_routine,
    std::uint32_t argument,
    std::uint32_t stack_base,
    std::uint32_t stack_size) const noexcept {
    const auto logical_id =
        runtime::A32LogicalThreadId::from_raw(pthread_id);
    if (!logical_id.has_value() || start_routine == 0U) {
        return std::nullopt;
    }

    const std::uint64_t stack_top64 =
        static_cast<std::uint64_t>(stack_base) + stack_size;
    if (stack_top64 == 0U ||
        stack_top64 > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    cpu::ExecutionRequest request{};
    const bool thumb = (start_routine & 1U) != 0U;
    request.instruction_set =
        thumb ? cpu::InstructionSet::Thumb : cpu::InstructionSet::Arm;
    request.entry_pc = start_routine & ~1U;
    request.regs[0] = argument;
    request.regs[13] =
        static_cast<std::uint32_t>(stack_top64) & ~7U;
    request.regs[14] = options_.exit_trampoline;
    request.instruction_count = options_.thread_instruction_budget;

    return runtime::make_a32_logical_execution_context(
        *logical_id,
        request);
}

bool A32PthreadLifecycleService::register_initial_thread(
    runtime::A32LogicalThreadId thread_id) noexcept {
    if (!configuration_valid() || !thread_id.valid()) return false;
    const std::uint32_t pthread_id = thread_id.value();
    if (find_thread(pthread_id) < threads_.size()) return true;

    const std::size_t slot = free_thread_slot();
    if (slot >= threads_.size()) return false;
    threads_[slot] = A32PthreadThreadState{
        .pthread_id = pthread_id,
        .initial_thread = true,
        .phase = A32PthreadThreadPhase::Running,
    };
    return true;
}

std::optional<A32PthreadCreatedThread>
A32PthreadLifecycleService::pop_created_thread() noexcept {
    std::optional<std::size_t> best;
    std::uint64_t best_sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        const auto& thread = threads_[index];
        if (thread.phase == A32PthreadThreadPhase::Running &&
            thread.start_pending &&
            thread.sequence < best_sequence) {
            best = index;
            best_sequence = thread.sequence;
        }
    }
    if (!best.has_value()) return std::nullopt;

    auto& thread = threads_[*best];
    thread.start_pending = false;
    return A32PthreadCreatedThread{
        .pthread_id = thread.pthread_id,
        .detached = thread.detached,
        .context = thread.context,
    };
}

runtime::A32HostServiceDisposition A32PthreadLifecycleService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (!is_lifecycle_svc(svc_immediate)) {
        return A32HostServiceDisposition::Unhandled;
    }
    if (!configuration_valid()) {
        return A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32PthreadAttrInitSvcImmediate) {
        const std::uint32_t address = regs[0];
        if (address == 0U) return A32HostServiceDisposition::Failed;
        const std::size_t index = ensure_attr(address);
        if (index >= attrs_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEnomem);
            return A32HostServiceDisposition::Handled;
        }
        attrs_[index] = A32PthreadAttrState{
            .address = address,
            .stack_size = options_.default_stack_size,
            .detached = false,
            .active = true,
        };
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadAttrDestroySvcImmediate) {
        const std::uint32_t address = regs[0];
        if (address == 0U) return A32HostServiceDisposition::Failed;
        const std::size_t index = find_attr(address);
        if (index < attrs_.size()) attrs_[index] = {};
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadAttrGetdetachstateSvcImmediate ||
        svc_immediate == kA32PthreadAttrSetdetachstateSvcImmediate ||
        svc_immediate == kA32PthreadAttrGetstacksizeSvcImmediate ||
        svc_immediate == kA32PthreadAttrSetstacksizeSvcImmediate) {
        const std::size_t index = find_attr(regs[0]);
        if (index >= attrs_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadAttrSetdetachstateSvcImmediate) {
            if (regs[1] != kA32PthreadCreateJoinable &&
                regs[1] != kA32PthreadCreateDetached) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
                return A32HostServiceDisposition::Handled;
            }
            attrs_[index].detached =
                regs[1] == kA32PthreadCreateDetached;
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadAttrGetdetachstateSvcImmediate) {
            if (regs[1] == 0U ||
                !write_u32_le(
                    memory,
                    regs[1],
                    attrs_[index].detached
                        ? kA32PthreadCreateDetached
                        : kA32PthreadCreateJoinable)) {
                return A32HostServiceDisposition::Failed;
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadAttrSetstacksizeSvcImmediate) {
            const std::uint64_t minimum =
                static_cast<std::uint64_t>(options_.page_size) * 2U;
            if (regs[1] < minimum) {
                regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
                return A32HostServiceDisposition::Handled;
            }
            attrs_[index].stack_size = regs[1];
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (regs[1] == 0U ||
            !write_u32_le(memory, regs[1], attrs_[index].stack_size)) {
            return A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadEqualSvcImmediate) {
        regs[0] = regs[0] == regs[1] ? 1U : 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (!current_thread_id_.valid()) {
        return A32HostServiceDisposition::Failed;
    }
    const std::size_t current =
        find_thread(current_thread_id_.value());
    if (current >= threads_.size() ||
        threads_[current].phase != A32PthreadThreadPhase::Running) {
        return A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32PthreadSelfSvcImmediate) {
        regs[0] = current_thread_id_.value();
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadExitSvcImmediate) {
        threads_[current].return_value = regs[0];
        threads_[current].start_pending = false;
        threads_[current].phase = A32PthreadThreadPhase::Exited;
        return A32HostServiceDisposition::Suspended;
    }

    if (svc_immediate != kA32PthreadCreateSvcImmediate) {
        return A32HostServiceDisposition::Unhandled;
    }

    const std::uint32_t thread_out = regs[0];
    const std::uint32_t attr_address = regs[1];
    const std::uint32_t start_routine = regs[2];
    const std::uint32_t argument = regs[3];
    if (thread_out == 0U) {
        return A32HostServiceDisposition::Failed;
    }
    if (start_routine == 0U) {
        regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
        return A32HostServiceDisposition::Handled;
    }

    std::uint32_t requested_stack_size = options_.default_stack_size;
    bool detached = false;
    if (attr_address != 0U) {
        const std::size_t attr = find_attr(attr_address);
        if (attr >= attrs_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }
        requested_stack_size = attrs_[attr].stack_size;
        detached = attrs_[attr].detached;
    }

    const std::size_t slot = free_thread_slot();
    if (slot >= threads_.size()) {
        regs[0] = static_cast<std::uint32_t>(kA32AndroidEagain);
        return A32HostServiceDisposition::Handled;
    }

    const auto pthread_id = choose_thread_id();
    std::uint32_t allocated_stack_size = 0U;
    const auto stack_base =
        choose_stack_base(requested_stack_size, allocated_stack_size);
    if (!pthread_id.has_value() || !stack_base.has_value()) {
        regs[0] = static_cast<std::uint32_t>(kA32AndroidEagain);
        return A32HostServiceDisposition::Handled;
    }

    const auto context = make_thread_context(
        *pthread_id,
        start_routine,
        argument,
        *stack_base,
        allocated_stack_size);
    if (!context.has_value()) {
        regs[0] = static_cast<std::uint32_t>(kA32AndroidEagain);
        return A32HostServiceDisposition::Handled;
    }

    if (!write_u32_le(memory, thread_out, *pthread_id)) {
        return A32HostServiceDisposition::Failed;
    }

    next_thread_id_ =
        *pthread_id == std::numeric_limits<std::uint32_t>::max()
            ? 0U
            : *pthread_id + 1U;
    const std::uint64_t sequence = next_sequence_;
    if (next_sequence_ != std::numeric_limits<std::uint64_t>::max()) {
        ++next_sequence_;
    }
    threads_[slot] = A32PthreadThreadState{
        .pthread_id = *pthread_id,
        .start_routine = start_routine,
        .argument = argument,
        .stack_base = *stack_base,
        .stack_size = allocated_stack_size,
        .detached = detached,
        .owns_stack = true,
        .initial_thread = false,
        .start_pending = true,
        .sequence = sequence,
        .phase = A32PthreadThreadPhase::Running,
        .context = *context,
    };
    regs[0] = 0U;
    return A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
