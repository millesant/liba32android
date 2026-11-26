#include "runtime/a32_logical_thread.h"

#include <utility>

namespace liba32android::runtime {

std::optional<A32LogicalExecutionContext>
make_a32_logical_execution_context(
    A32LogicalThreadId thread_id,
    cpu::ExecutionRequest request) noexcept {
    A32LogicalExecutionContext context{
        .thread_id = thread_id,
        .request = std::move(request),
    };
    if (!context.valid()) return std::nullopt;
    return context;
}

std::optional<A32LogicalExecutionContext>
make_a32_service_resume_context(
    A32LogicalThreadId thread_id,
    const A32ServiceDispatchResult& suspended_result,
    std::size_t instruction_budget,
    std::optional<std::uint32_t> stop_pc) {
    if (!thread_id.valid()) return std::nullopt;

    auto request = make_a32_service_resume_request(
        suspended_result,
        instruction_budget,
        stop_pc);
    if (!request.has_value()) return std::nullopt;

    return make_a32_logical_execution_context(
        thread_id,
        std::move(*request));
}

}  // namespace liba32android::runtime
