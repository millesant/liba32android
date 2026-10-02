#include "compat/a32_pthread_lifecycle.h"

#include <algorithm>
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
    case kA32PthreadJoinSvcImmediate:
    case kA32PthreadDetachSvcImmediate:
    case kA32PthreadGetschedparamSvcImmediate:
    case kA32PthreadSetschedparamSvcImmediate:
    case kA32PthreadSetnameNpSvcImmediate:
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

[[nodiscard]] bool read_u32_le(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] bool readable_u32(
    const memory::GuestMemory& memory,
    std::uint32_t address) {
    std::array<std::uint8_t, 4> bytes{};
    return memory.read(address, bytes);
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

[[nodiscard]] bool valid_guest_function(
    std::uint32_t function,
    std::uint32_t return_pc) noexcept {
    if (function == 0U ||
        function == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const bool thumb = (function & 1U) != 0U;
    const std::uint32_t entry_pc = function & ~1U;
    return entry_pc != return_pc &&
           (thumb || (entry_pc & 3U) == 0U);
}

class SyncThreadScope final {
public:
    SyncThreadScope(
        A32PthreadSyncService& service,
        runtime::A32LogicalThreadId thread_id) noexcept
        : service_(service),
          previous_(service.logical_thread_id()) {
        service_.set_current_thread_id(thread_id.value());
    }

    ~SyncThreadScope() {
        service_.set_current_thread_id(previous_.value());
    }

    SyncThreadScope(const SyncThreadScope&) = delete;
    SyncThreadScope& operator=(const SyncThreadScope&) = delete;

private:
    A32PthreadSyncService& service_;
    runtime::A32LogicalThreadId previous_{};
};

[[nodiscard]] A32PthreadExitCleanupError cleanup_error_from_dispatch(
    runtime::A32ServiceDispatchError error) noexcept {
    switch (error) {
    case runtime::A32ServiceDispatchError::None:
        return A32PthreadExitCleanupError::None;
    case runtime::A32ServiceDispatchError::MemoryFault:
        return A32PthreadExitCleanupError::MemoryFault;
    case runtime::A32ServiceDispatchError::CpuException:
        return A32PthreadExitCleanupError::CpuException;
    case runtime::A32ServiceDispatchError::ServiceLimitExceeded:
        return A32PthreadExitCleanupError::ServiceLimitExceeded;
    case runtime::A32ServiceDispatchError::ServiceUnhandled:
        return A32PthreadExitCleanupError::ServiceUnhandled;
    case runtime::A32ServiceDispatchError::ServiceFailed:
        return A32PthreadExitCleanupError::ServiceFailed;
    case runtime::A32ServiceDispatchError::InstructionLimitExceeded:
        return A32PthreadExitCleanupError::InstructionLimitExceeded;
    }
    return A32PthreadExitCleanupError::ServiceFailed;
}

}  // namespace

A32PthreadLifecycleService::A32PthreadLifecycleService(
    A32PthreadLifecycleOptions options,
    std::span<A32PthreadAttrState> attrs,
    std::span<A32PthreadThreadState> threads,
    A32PthreadSyncService* sync_service,
    A32PthreadThreadExitHook* exit_hook) noexcept
    : options_(options),
      attrs_(attrs),
      threads_(threads),
      sync_service_(sync_service),
      exit_hook_(exit_hook),
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
    if (arena_end > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    if (sync_service_ != nullptr) {
        if (options_.tls_destructor_return_pc == 0U ||
            (options_.tls_destructor_return_pc & 3U) != 0U ||
            options_.tls_destructor_instruction_budget == 0U ||
            options_.tls_destructor_service_limit == 0U) {
            return false;
        }
    }
    return true;
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

void A32PthreadLifecycleService::reclaim_thread(
    std::size_t index) noexcept {
    if (index >= threads_.size()) return;
    const std::uint32_t pthread_id = threads_[index].pthread_id;
    if (sync_service_ != nullptr && pthread_id != 0U) {
        sync_service_->clear_tls_values_for_thread(pthread_id);
        if (sync_service_->current_thread_id() == pthread_id) {
            sync_service_->set_current_thread_id(0U);
        }
    }
    if (current_thread_id_.value() == pthread_id) {
        current_thread_id_ = {};
    }
    threads_[index] = {};
}

std::optional<A32PthreadJoinWake>
A32PthreadLifecycleService::pop_ready_join() noexcept {
    std::optional<std::size_t> best;
    std::uint64_t best_sequence = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        const auto& thread = threads_[index];
        if (thread.phase == A32PthreadThreadPhase::Exited &&
            thread.join_wake_ready &&
            thread.join_wake_sequence < best_sequence) {
            best = index;
            best_sequence = thread.join_wake_sequence;
        }
    }
    if (!best.has_value()) return std::nullopt;

    const A32PthreadJoinWake wake{
        .thread_id = threads_[*best].joiner_thread_id,
        .target_thread_id = threads_[*best].pthread_id,
    };
    reclaim_thread(*best);
    return wake;
}

A32PthreadExitCleanupResult
A32PthreadLifecycleService::run_exit_cleanup(
    memory::GuestMemory& memory,
    A32PthreadThreadState& thread,
    std::uint32_t stack_pointer) {
    A32PthreadExitCleanupResult result{
        .thread_id = thread.pthread_id,
    };

    if (sync_service_ != nullptr) {
        if (stack_pointer == 0U || (stack_pointer & 7U) != 0U) {
            result.error = A32PthreadExitCleanupError::InvalidStack;
            return result;
        }
        const auto logical_id =
            runtime::A32LogicalThreadId::from_raw(thread.pthread_id);
        if (!logical_id.has_value()) {
            result.error = A32PthreadExitCleanupError::ServiceFailed;
            return result;
        }

        SyncThreadScope thread_scope{*sync_service_, *logical_id};
        for (std::uint32_t round = 0U;
             round < kA32PthreadDestructorIterations;
             ++round) {
            bool called_in_round = false;
            for (std::size_t key_slot = 0U;
                 key_slot < sync_service_->tls_key_slot_count();
                 ++key_slot) {
                auto call = sync_service_->take_tls_destructor(
                    thread.pthread_id, key_slot);
                if (!call.has_value()) continue;
                called_in_round = true;

                if (!valid_guest_function(
                        call->destructor,
                        options_.tls_destructor_return_pc)) {
                    result.error =
                        A32PthreadExitCleanupError::InvalidDestructorAddress;
                    result.failing_key = call->key;
                    return result;
                }

                const bool thumb = (call->destructor & 1U) != 0U;
                cpu::ExecutionRequest request{};
                request.instruction_set =
                    thumb ? cpu::InstructionSet::Thumb
                          : cpu::InstructionSet::Arm;
                request.entry_pc = call->destructor & ~1U;
                request.regs[0] = call->value;
                request.regs[13] = stack_pointer;
                request.regs[14] =
                    options_.tls_destructor_return_pc |
                    (thumb ? 1U : 0U);
                request.instruction_count =
                    options_.tls_destructor_instruction_budget;
                request.stop_pc = options_.tls_destructor_return_pc;

                auto callback = runtime::execute_a32_with_services(
                    memory,
                    request,
                    *sync_service_,
                    options_.tls_destructor_service_limit);
                if (callback.service_suspended) {
                    result.error =
                        A32PthreadExitCleanupError::ServiceSuspended;
                    result.failing_key = call->key;
                    result.failing_svc_immediate =
                        callback.suspended_svc_immediate;
                    return result;
                }
                if (!callback) {
                    result.error =
                        cleanup_error_from_dispatch(callback.error);
                    result.failing_key = call->key;
                    result.failing_svc_immediate =
                        callback.failing_svc_immediate;
                    return result;
                }
                if (!callback.stop_pc_reached) {
                    result.error =
                        A32PthreadExitCleanupError::InstructionLimitExceeded;
                    result.failing_key = call->key;
                    return result;
                }
                ++result.callbacks_completed;
            }

            if (!called_in_round) break;
            result.iterations_completed = round + 1U;
        }

        // POSIX/Bionic stop after the finite destructor-iteration ceiling.
        // Remaining repopulated values disappear with the thread state.
        sync_service_->clear_tls_values_for_thread(thread.pthread_id);
    }

    if (exit_hook_ != nullptr) {
        const auto logical_id =
            runtime::A32LogicalThreadId::from_raw(thread.pthread_id);
        if (!logical_id.has_value() ||
            !exit_hook_->on_thread_exit(memory, *logical_id)) {
            result.error = A32PthreadExitCleanupError::ExitHookFailed;
            return result;
        }
    }

    return result;
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

    if (svc_immediate == kA32PthreadGetschedparamSvcImmediate ||
        svc_immediate == kA32PthreadSetschedparamSvcImmediate ||
        svc_immediate == kA32PthreadSetnameNpSvcImmediate) {
        const std::size_t target = find_thread(regs[0]);
        if (target >= threads_.size() ||
            threads_[target].phase != A32PthreadThreadPhase::Running) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEsrch);
            return A32HostServiceDisposition::Handled;
        }
        auto& target_thread = threads_[target];

        if (svc_immediate == kA32PthreadGetschedparamSvcImmediate) {
            if (regs[1] == 0U || regs[2] == 0U ||
                !write_u32_le(
                    memory,
                    regs[1],
                    std::bit_cast<std::uint32_t>(target_thread.sched_policy)) ||
                !write_u32_le(
                    memory,
                    regs[2],
                    std::bit_cast<std::uint32_t>(target_thread.sched_priority))) {
                return A32HostServiceDisposition::Failed;
            }
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32PthreadSetschedparamSvcImmediate) {
            std::uint32_t priority_word{};
            if (regs[2] == 0U ||
                !read_u32_le(memory, regs[2], priority_word)) {
                return A32HostServiceDisposition::Failed;
            }
            const std::int32_t policy = std::bit_cast<std::int32_t>(regs[1]);
            const std::int32_t priority =
                std::bit_cast<std::int32_t>(priority_word);
            if (policy == kA32SchedOther) {
                if (priority != 0) {
                    regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
                    return A32HostServiceDisposition::Handled;
                }
                target_thread.sched_policy = policy;
                target_thread.sched_priority = priority;
                regs[0] = 0U;
                return A32HostServiceDisposition::Handled;
            }
            // The cooperative runtime has no host scheduler identity. Keep the
            // accepted surface at SCHED_OTHER/0 rather than faking policy
            // changes against an unrelated host thread.
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEperm);
            return A32HostServiceDisposition::Handled;
        }

        if (regs[1] == 0U) {
            return A32HostServiceDisposition::Failed;
        }
        std::array<std::uint8_t, kA32PthreadNameBytes> name{};
        bool terminated = false;
        for (std::size_t index = 0; index < name.size(); ++index) {
            if (regs[1] >
                std::numeric_limits<std::uint32_t>::max() - index) {
                return A32HostServiceDisposition::Failed;
            }
            std::array<std::uint8_t, 1> byte{};
            if (!memory.read(
                    regs[1] + static_cast<std::uint32_t>(index), byte)) {
                return A32HostServiceDisposition::Failed;
            }
            name[index] = byte[0];
            if (byte[0] == 0U) {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidErange);
            return A32HostServiceDisposition::Handled;
        }
        target_thread.name = name;
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadSelfSvcImmediate) {
        regs[0] = current_thread_id_.value();
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadJoinSvcImmediate) {
        const std::uint32_t target_id = regs[0];
        const std::uint32_t result_address = regs[1];
        if (target_id == current_thread_id_.value()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEdeadlk);
            return A32HostServiceDisposition::Handled;
        }

        const std::size_t target = find_thread(target_id);
        if (target >= threads_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEsrch);
            return A32HostServiceDisposition::Handled;
        }
        auto& target_thread = threads_[target];
        if (target_thread.detached || target_thread.join_claimed ||
            target_thread.phase == A32PthreadThreadPhase::CleanupFailed ||
            target_thread.phase == A32PthreadThreadPhase::Cleaning) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }
        if (result_address != 0U &&
            !readable_u32(memory, result_address)) {
            return A32HostServiceDisposition::Failed;
        }

        if (target_thread.phase == A32PthreadThreadPhase::Exited) {
            if (result_address != 0U &&
                !write_u32_le(
                    memory,
                    result_address,
                    target_thread.return_value)) {
                return A32HostServiceDisposition::Failed;
            }
            reclaim_thread(target);
            regs[0] = 0U;
            return A32HostServiceDisposition::Handled;
        }

        target_thread.join_claimed = true;
        target_thread.joiner_thread_id = current_thread_id_.value();
        target_thread.join_result_address = result_address;
        regs[0] = 0U;
        return A32HostServiceDisposition::Suspended;
    }

    if (svc_immediate == kA32PthreadDetachSvcImmediate) {
        const std::uint32_t target_id = regs[0];
        const std::size_t target = find_thread(target_id);
        if (target >= threads_.size()) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEsrch);
            return A32HostServiceDisposition::Handled;
        }

        auto& target_thread = threads_[target];
        if (target_thread.detached || target_thread.join_claimed ||
            target_thread.phase == A32PthreadThreadPhase::CleanupFailed ||
            target_thread.phase == A32PthreadThreadPhase::Cleaning) {
            regs[0] = static_cast<std::uint32_t>(kA32AndroidEinval);
            return A32HostServiceDisposition::Handled;
        }

        if (target_thread.phase == A32PthreadThreadPhase::Exited) {
            reclaim_thread(target);
        } else {
            target_thread.detached = true;
        }
        regs[0] = 0U;
        return A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32PthreadExitSvcImmediate) {
        auto& thread = threads_[current];
        thread.return_value = regs[0];
        thread.start_pending = false;
        thread.phase = A32PthreadThreadPhase::Cleaning;

        last_exit_cleanup_result_ =
            run_exit_cleanup(memory, thread, regs[13]);
        if (!*last_exit_cleanup_result_) {
            thread.phase = A32PthreadThreadPhase::CleanupFailed;
            return A32HostServiceDisposition::Failed;
        }

        thread.phase = A32PthreadThreadPhase::Exited;
        if (thread.join_claimed) {
            if (thread.join_result_address != 0U &&
                !write_u32_le(
                    memory,
                    thread.join_result_address,
                    thread.return_value)) {
                last_exit_cleanup_result_->error =
                    A32PthreadExitCleanupError::JoinResultWriteFailed;
                thread.phase = A32PthreadThreadPhase::CleanupFailed;
                return A32HostServiceDisposition::Failed;
            }
            thread.join_wake_ready = true;
            thread.join_wake_sequence = next_join_wake_sequence_;
            if (next_join_wake_sequence_ !=
                std::numeric_limits<std::uint64_t>::max()) {
                ++next_join_wake_sequence_;
            }
        } else if (thread.detached) {
            reclaim_thread(current);
        }
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

const char* to_string(A32PthreadExitCleanupError error) noexcept {
    switch (error) {
    case A32PthreadExitCleanupError::None: return "none";
    case A32PthreadExitCleanupError::InvalidStack: return "invalid_stack";
    case A32PthreadExitCleanupError::InvalidDestructorAddress:
        return "invalid_destructor_address";
    case A32PthreadExitCleanupError::MemoryFault: return "memory_fault";
    case A32PthreadExitCleanupError::CpuException: return "cpu_exception";
    case A32PthreadExitCleanupError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32PthreadExitCleanupError::ServiceUnhandled:
        return "service_unhandled";
    case A32PthreadExitCleanupError::ServiceFailed:
        return "service_failed";
    case A32PthreadExitCleanupError::ServiceSuspended:
        return "service_suspended";
    case A32PthreadExitCleanupError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    case A32PthreadExitCleanupError::ExitHookFailed:
        return "exit_hook_failed";
    case A32PthreadExitCleanupError::JoinResultWriteFailed:
        return "join_result_write_failed";
    }
    return "unknown";
}

}  // namespace liba32android::compat
