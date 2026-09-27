#include "elf/elf32_lifecycle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace liba32android::elf {
namespace {

[[nodiscard]] Elf32FunctionArrayDecodeResult failure(
    Elf32FunctionArrayDecodeError error) {
    Elf32FunctionArrayDecodeResult result;
    result.error = error;
    return result;
}

}  // namespace

Elf32FunctionArrayDecodeResult decode_elf32_function_array(
    const memory::GuestMemory& memory,
    const Elf32FunctionArrayMetadata& array,
    const Elf32FunctionArrayDecodeOptions& options) {
    constexpr std::uint32_t kEntrySize = 4;
    constexpr std::uint64_t kGuestAddressSpaceSize = std::uint64_t{1} << 32;

    if ((array.size % kEntrySize) != 0) {
        return failure(Elf32FunctionArrayDecodeError::InvalidArraySize);
    }

    const std::uint32_t entry_count = array.size / kEntrySize;
    if (entry_count > options.max_entries) {
        return failure(Elf32FunctionArrayDecodeError::TooManyEntries);
    }

    if (static_cast<std::uint64_t>(array.guest_address) +
            static_cast<std::uint64_t>(array.size) >
        kGuestAddressSpaceSize) {
        return failure(Elf32FunctionArrayDecodeError::RangeOverflow);
    }

    Elf32FunctionArrayDecodeResult result;
    result.entries.reserve(entry_count);
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        const std::uint64_t address =
            static_cast<std::uint64_t>(array.guest_address) +
            static_cast<std::uint64_t>(index) * kEntrySize;
        if (address > std::numeric_limits<std::uint32_t>::max()) {
            return failure(Elf32FunctionArrayDecodeError::RangeOverflow);
        }

        std::array<std::uint8_t, kEntrySize> bytes{};
        if (!memory.read(static_cast<std::uint32_t>(address), bytes)) {
            return failure(Elf32FunctionArrayDecodeError::ReadFailed);
        }
        result.entries.push_back(
            static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U));
    }
    return result;
}

const char* to_string(Elf32FunctionArrayDecodeError error) noexcept {
    switch (error) {
    case Elf32FunctionArrayDecodeError::None: return "none";
    case Elf32FunctionArrayDecodeError::InvalidArraySize:
        return "invalid_array_size";
    case Elf32FunctionArrayDecodeError::RangeOverflow:
        return "range_overflow";
    case Elf32FunctionArrayDecodeError::TooManyEntries:
        return "too_many_entries";
    case Elf32FunctionArrayDecodeError::ReadFailed:
        return "read_failed";
    }
    return "unknown";
}

namespace {

enum class InitVisitState : std::uint8_t {
    Unseen = 0,
    Visiting,
    Complete,
};

[[nodiscard]] Elf32InitPlanResult plan_failure(
    Elf32InitPlanError error,
    std::optional<std::size_t> failing_object = std::nullopt,
    Elf32FunctionArrayDecodeError decode_error =
        Elf32FunctionArrayDecodeError::None) {
    Elf32InitPlanResult result;
    result.error = error;
    result.decode_error = decode_error;
    result.failing_object = failing_object;
    return result;
}

struct InitPlanContext {
    const memory::GuestMemory& memory;
    const Elf32DependencyGraph& graph;
    const Elf32InitPlanOptions& options;
    std::vector<InitVisitState> states;
    std::uint32_t visited_objects{};
    std::uint32_t decoded_entries{};
    std::vector<Elf32InitCall> calls;

    [[nodiscard]] Elf32InitPlanResult visit(std::size_t object_index) {
        if (states[object_index] == InitVisitState::Complete ||
            states[object_index] == InitVisitState::Visiting) {
            return {};
        }
        if (visited_objects >= options.max_objects) {
            return plan_failure(Elf32InitPlanError::ObjectLimitExceeded,
                                object_index);
        }

        ++visited_objects;
        states[object_index] = InitVisitState::Visiting;
        const auto& object = graph.objects[object_index];

        for (const auto& edge : object.dependencies) {
            if (edge.target_object >= graph.objects.size()) {
                return plan_failure(Elf32InitPlanError::InvalidGraphEdge,
                                    object_index);
            }
            auto nested = visit(edge.target_object);
            if (!nested) return nested;
        }

        if (object.linker_metadata.init_array.has_value()) {
            const std::uint32_t remaining =
                options.max_entries - decoded_entries;
            const auto decoded = decode_elf32_function_array(
                memory, *object.linker_metadata.init_array,
                Elf32FunctionArrayDecodeOptions{.max_entries = remaining});
            if (!decoded) {
                const Elf32InitPlanError error =
                    decoded.error ==
                            Elf32FunctionArrayDecodeError::TooManyEntries
                        ? Elf32InitPlanError::EntryLimitExceeded
                        : Elf32InitPlanError::DecodeFailed;
                return plan_failure(error, object_index, decoded.error);
            }

            decoded_entries +=
                static_cast<std::uint32_t>(decoded.entries.size());
            for (std::size_t index = 0; index < decoded.entries.size();
                 ++index) {
                const std::uint32_t function = decoded.entries[index];
                if (function == 0U ||
                    function == std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                calls.push_back(Elf32InitCall{
                    .object_index = object_index,
                    .array_index = static_cast<std::uint32_t>(index),
                    .function = function,
                });
            }
        }

        states[object_index] = InitVisitState::Complete;
        return {};
    }
};

}  // namespace

Elf32InitPlanResult plan_elf32_init_array_calls(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t root_object,
    const Elf32InitPlanOptions& options) {
    if (options.max_objects == 0) {
        return plan_failure(Elf32InitPlanError::InvalidOptions);
    }
    if (root_object >= graph.objects.size()) {
        return plan_failure(Elf32InitPlanError::InvalidRootObject,
                            root_object);
    }

    InitPlanContext context{
        .memory = memory,
        .graph = graph,
        .options = options,
        .states =
            std::vector<InitVisitState>(graph.objects.size(),
                                        InitVisitState::Unseen),
    };
    auto result = context.visit(root_object);
    if (!result) return result;
    result.calls = std::move(context.calls);
    return result;
}

const char* to_string(Elf32InitPlanError error) noexcept {
    switch (error) {
    case Elf32InitPlanError::None: return "none";
    case Elf32InitPlanError::InvalidOptions: return "invalid_options";
    case Elf32InitPlanError::InvalidRootObject: return "invalid_root_object";
    case Elf32InitPlanError::InvalidGraphEdge: return "invalid_graph_edge";
    case Elf32InitPlanError::ObjectLimitExceeded:
        return "object_limit_exceeded";
    case Elf32InitPlanError::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case Elf32InitPlanError::DecodeFailed: return "decode_failed";
    }
    return "unknown";
}


namespace {

enum class FiniVisitState : std::uint8_t {
    Unseen = 0,
    Visiting,
    Complete,
};

[[nodiscard]] Elf32FiniPlanResult fini_plan_failure(
    Elf32FiniPlanError error,
    std::optional<std::size_t> failing_object = std::nullopt,
    Elf32FunctionArrayDecodeError decode_error =
        Elf32FunctionArrayDecodeError::None) {
    Elf32FiniPlanResult result;
    result.error = error;
    result.decode_error = decode_error;
    result.failing_object = failing_object;
    return result;
}

struct FiniPlanContext {
    const Elf32DependencyGraph& graph;
    const Elf32FiniPlanOptions& options;
    std::vector<FiniVisitState> states;
    std::uint32_t visited_objects{};
    std::vector<std::size_t> postorder;

    [[nodiscard]] Elf32FiniPlanResult visit(std::size_t object_index) {
        if (states[object_index] == FiniVisitState::Complete ||
            states[object_index] == FiniVisitState::Visiting) {
            return {};
        }
        if (visited_objects >= options.max_objects) {
            return fini_plan_failure(
                Elf32FiniPlanError::ObjectLimitExceeded, object_index);
        }

        ++visited_objects;
        states[object_index] = FiniVisitState::Visiting;
        const auto& object = graph.objects[object_index];
        for (const auto& edge : object.dependencies) {
            if (edge.target_object >= graph.objects.size()) {
                return fini_plan_failure(
                    Elf32FiniPlanError::InvalidGraphEdge, object_index);
            }
            auto nested = visit(edge.target_object);
            if (!nested) return nested;
        }

        states[object_index] = FiniVisitState::Complete;
        postorder.push_back(object_index);
        return {};
    }
};

}  // namespace

Elf32FiniPlanResult plan_elf32_fini_array_calls(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t root_object,
    const Elf32FiniPlanOptions& options) {
    if (options.max_objects == 0) {
        return fini_plan_failure(Elf32FiniPlanError::InvalidOptions);
    }
    if (root_object >= graph.objects.size()) {
        return fini_plan_failure(
            Elf32FiniPlanError::InvalidRootObject, root_object);
    }

    FiniPlanContext context{
        .graph = graph,
        .options = options,
        .states = std::vector<FiniVisitState>(
            graph.objects.size(), FiniVisitState::Unseen),
    };
    auto traversal = context.visit(root_object);
    if (!traversal) return traversal;

    std::uint32_t decoded_entries{};
    std::vector<Elf32FiniCall> calls;
    for (auto object_it = context.postorder.rbegin();
         object_it != context.postorder.rend(); ++object_it) {
        const std::size_t object_index = *object_it;
        const auto& object = graph.objects[object_index];
        if (!object.linker_metadata.fini_array.has_value()) {
            continue;
        }

        const std::uint32_t remaining =
            options.max_entries - decoded_entries;
        const auto decoded = decode_elf32_function_array(
            memory, *object.linker_metadata.fini_array,
            Elf32FunctionArrayDecodeOptions{.max_entries = remaining});
        if (!decoded) {
            const Elf32FiniPlanError error =
                decoded.error ==
                        Elf32FunctionArrayDecodeError::TooManyEntries
                    ? Elf32FiniPlanError::EntryLimitExceeded
                    : Elf32FiniPlanError::DecodeFailed;
            return fini_plan_failure(error, object_index, decoded.error);
        }

        decoded_entries +=
            static_cast<std::uint32_t>(decoded.entries.size());
        for (std::size_t reverse_index = decoded.entries.size();
             reverse_index > 0; --reverse_index) {
            const std::size_t index = reverse_index - 1U;
            const std::uint32_t function = decoded.entries[index];
            if (function == 0U ||
                function == std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            calls.push_back(Elf32FiniCall{
                .object_index = object_index,
                .array_index = static_cast<std::uint32_t>(index),
                .function = function,
            });
        }
    }

    Elf32FiniPlanResult result;
    result.calls = std::move(calls);
    return result;
}

const char* to_string(Elf32FiniPlanError error) noexcept {
    switch (error) {
    case Elf32FiniPlanError::None: return "none";
    case Elf32FiniPlanError::InvalidOptions: return "invalid_options";
    case Elf32FiniPlanError::InvalidRootObject: return "invalid_root_object";
    case Elf32FiniPlanError::InvalidGraphEdge: return "invalid_graph_edge";
    case Elf32FiniPlanError::ObjectLimitExceeded:
        return "object_limit_exceeded";
    case Elf32FiniPlanError::EntryLimitExceeded:
        return "entry_limit_exceeded";
    case Elf32FiniPlanError::DecodeFailed: return "decode_failed";
    }
    return "unknown";
}

namespace {

[[nodiscard]] Elf32InitExecutionResult execution_failure(
    Elf32InitExecutionError error,
    std::size_t calls_completed,
    std::size_t failing_call,
    std::size_t failing_object,
    std::optional<cpu::ExecutionResult> cpu_result = std::nullopt) {
    Elf32InitExecutionResult result;
    result.error = error;
    result.calls_completed = calls_completed;
    result.failing_call = failing_call;
    result.failing_object = failing_object;
    result.cpu_result = std::move(cpu_result);
    return result;
}

}  // namespace

Elf32InitExecutionResult execute_elf32_init_calls(
    memory::GuestMemory& memory,
    std::span<const Elf32InitCall> calls,
    const Elf32InitExecutionOptions& options) {
    if (options.stack_top == 0U ||
        (options.stack_top & 7U) != 0U ||
        (options.return_pc & 3U) != 0U ||
        options.max_instructions_per_call == 0U) {
        Elf32InitExecutionResult result;
        result.error = Elf32InitExecutionError::InvalidOptions;
        return result;
    }

    Elf32InitExecutionResult result;
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        const bool thumb = (call.function & 1U) != 0U;
        const std::uint32_t entry_pc = call.function & ~1U;
        if (call.function == 0U ||
            call.function == std::numeric_limits<std::uint32_t>::max() ||
            entry_pc == options.return_pc ||
            (!thumb && (entry_pc & 3U) != 0U)) {
            return execution_failure(
                Elf32InitExecutionError::InvalidFunctionAddress,
                result.calls_completed, index, call.object_index);
        }

        cpu::ExecutionRequest request{};
        request.instruction_set =
            thumb ? cpu::InstructionSet::Thumb : cpu::InstructionSet::Arm;
        request.entry_pc = entry_pc;
        request.regs[13] = options.stack_top;
        request.regs[14] = options.return_pc | (thumb ? 1U : 0U);
        request.instruction_count = options.max_instructions_per_call;
        request.stop_pc = options.return_pc;

        auto cpu_result = cpu::execute(memory, request);
        // Instruction-fetch/data faults can also make the CPU backend report a
        // generic exception. Preserve the more specific memory-fault cause.
        if (cpu_result.memory_fault) {
            return execution_failure(
                Elf32InitExecutionError::MemoryFault,
                result.calls_completed, index, call.object_index,
                std::move(cpu_result));
        }
        if (cpu_result.exception_raised) {
            return execution_failure(
                Elf32InitExecutionError::CpuException,
                result.calls_completed, index, call.object_index,
                std::move(cpu_result));
        }
        if (!cpu_result.stop_pc_reached) {
            return execution_failure(
                Elf32InitExecutionError::InstructionLimitExceeded,
                result.calls_completed, index, call.object_index,
                std::move(cpu_result));
        }

        ++result.calls_completed;
    }
    return result;
}

const char* to_string(Elf32InitExecutionError error) noexcept {
    switch (error) {
    case Elf32InitExecutionError::None: return "none";
    case Elf32InitExecutionError::InvalidOptions:
        return "invalid_options";
    case Elf32InitExecutionError::InvalidFunctionAddress:
        return "invalid_function_address";
    case Elf32InitExecutionError::CpuException:
        return "cpu_exception";
    case Elf32InitExecutionError::MemoryFault:
        return "memory_fault";
    case Elf32InitExecutionError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    }
    return "unknown";
}

Elf32FiniExecutionResult execute_elf32_fini_calls(
    memory::GuestMemory& memory,
    std::span<const Elf32FiniCall> calls,
    const Elf32FiniExecutionOptions& options) {
    return execute_elf32_init_calls(memory, calls, options);
}

}  // namespace liba32android::elf
