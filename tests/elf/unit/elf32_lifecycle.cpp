#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "elf/elf32_lifecycle.h"
#include "memory/guest_memory.h"
#include "runtime/a32_service_dispatch.h"
#include "runtime/a32_service_registry.h"

namespace {

using liba32android::elf::Elf32DependencyEdge;
using liba32android::elf::Elf32DependencyGraph;
using liba32android::elf::Elf32FunctionArrayDecodeError;
using liba32android::elf::Elf32FunctionArrayDecodeOptions;
using liba32android::elf::Elf32FunctionArrayMetadata;
using liba32android::elf::Elf32FiniExecutionOptions;
using liba32android::elf::Elf32FiniPlanError;
using liba32android::elf::Elf32FiniPlanOptions;
using liba32android::elf::Elf32InitCall;
using liba32android::elf::Elf32InitExecutionError;
using liba32android::elf::Elf32InitExecutionOptions;
using liba32android::elf::Elf32InitPlanError;
using liba32android::elf::Elf32InitPlanOptions;
using liba32android::elf::Elf32LifecycleExecutionContext;
using liba32android::elf::Elf32LifecycleObjectStatus;
using liba32android::elf::Elf32LifecycleState;
using liba32android::elf::Elf32PersistentLifecycleError;
using liba32android::elf::Elf32PersistentLifecycleOptions;
using liba32android::elf::decode_elf32_function_array;
using liba32android::elf::execute_elf32_fini_calls;
using liba32android::elf::execute_elf32_init_calls;
using liba32android::elf::plan_elf32_fini_array_calls;
using liba32android::elf::plan_elf32_init_array_calls;
using liba32android::elf::run_elf32_persistent_constructors;
using liba32android::elf::run_elf32_persistent_destructors;
using liba32android::memory::LinearGuestMemory;
using liba32android::runtime::A32HostServiceDisposition;
using liba32android::runtime::A32HostServiceHandler;
using liba32android::runtime::A32HostServiceRegistry;
using liba32android::runtime::A32HostServiceRegistryEntry;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool write_u32(LinearGuestMemory& memory,
               std::uint32_t address,
               std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
    return memory.write(address, bytes);
}

int test_exact_decode_and_raw_sentinels() {
    LinearGuestMemory memory(0x100, 0x1000);
    constexpr std::uint32_t address = 0x1010;
    if (!write_u32(memory, address, 0U) ||
        !write_u32(memory, address + 4U, 0xffffffffU) ||
        !write_u32(memory, address + 8U, 0x12345679U)) {
        return fail("could not stage lifecycle array");
    }

    std::array<std::uint8_t, 12> before{};
    if (!memory.read(address, before)) {
        return fail("could not snapshot lifecycle array");
    }

    const auto result = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{.guest_address = address, .size = 12},
        Elf32FunctionArrayDecodeOptions{.max_entries = 3});
    if (!result ||
        result.entries !=
            std::vector<std::uint32_t>{0U, 0xffffffffU, 0x12345679U}) {
        return fail("lifecycle array decode changed order or sentinel values");
    }

    std::array<std::uint8_t, 12> after{};
    if (!memory.read(address, after) || after != before) {
        return fail("lifecycle array decode mutated guest memory");
    }
    return 0;
}

int test_entry_ceiling_preflights_before_read() {
    LinearGuestMemory memory(0x100, 0x1000);
    const auto result = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{
            .guest_address = 0x90000000U,
            .size = 12,
        },
        Elf32FunctionArrayDecodeOptions{.max_entries = 2});
    if (result.error != Elf32FunctionArrayDecodeError::TooManyEntries ||
        !result.entries.empty()) {
        return fail("lifecycle entry ceiling did not fail before reading");
    }
    return 0;
}

int test_invalid_size_and_range() {
    LinearGuestMemory memory(0x100, 0x1000);

    const auto bad_size = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{.guest_address = 0x1000, .size = 6},
        Elf32FunctionArrayDecodeOptions{.max_entries = 2});
    if (bad_size.error != Elf32FunctionArrayDecodeError::InvalidArraySize) {
        return fail("non-integral lifecycle array size was not rejected");
    }

    const auto overflow = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{
            .guest_address = 0xfffffffcU,
            .size = 8,
        },
        Elf32FunctionArrayDecodeOptions{.max_entries = 2});
    if (overflow.error != Elf32FunctionArrayDecodeError::RangeOverflow) {
        return fail("lifecycle decoder range overflow was not rejected");
    }
    return 0;
}

int test_unreadable_and_empty_arrays() {
    LinearGuestMemory memory(0x100, 0x1000);

    const auto unreadable = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{.guest_address = 0x2000, .size = 4},
        Elf32FunctionArrayDecodeOptions{.max_entries = 1});
    if (unreadable.error != Elf32FunctionArrayDecodeError::ReadFailed ||
        !unreadable.entries.empty()) {
        return fail("unreadable lifecycle array was not rejected cleanly");
    }

    const auto empty = decode_elf32_function_array(
        memory,
        Elf32FunctionArrayMetadata{
            .guest_address = 0xffffffffU,
            .size = 0,
        },
        Elf32FunctionArrayDecodeOptions{.max_entries = 0});
    if (!empty || !empty.entries.empty()) {
        return fail("zero-length lifecycle array did not decode as empty");
    }
    return 0;
}

bool stage_init_array(LinearGuestMemory& memory,
                      Elf32DependencyGraph& graph,
                      std::size_t object_index,
                      std::uint32_t address,
                      const std::vector<std::uint32_t>& entries) {
    if (object_index >= graph.objects.size()) return false;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!write_u32(memory,
                       address + static_cast<std::uint32_t>(index * 4U),
                       entries[index])) {
            return false;
        }
    }
    graph.objects[object_index].linker_metadata.init_array =
        Elf32FunctionArrayMetadata{
            .guest_address = address,
            .size = static_cast<std::uint32_t>(entries.size() * 4U),
        };
    return true;
}


bool stage_fini_array(LinearGuestMemory& memory,
                      Elf32DependencyGraph& graph,
                      std::size_t object_index,
                      std::uint32_t address,
                      const std::vector<std::uint32_t>& entries) {
    if (object_index >= graph.objects.size()) return false;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!write_u32(memory,
                       address + static_cast<std::uint32_t>(index * 4U),
                       entries[index])) {
            return false;
        }
    }
    graph.objects[object_index].linker_metadata.fini_array =
        Elf32FunctionArrayMetadata{
            .guest_address = address,
            .size = static_cast<std::uint32_t>(entries.size() * 4U),
        };
    return true;
}

int test_init_plan_dependency_order_cycles_and_sentinels() {
    LinearGuestMemory memory(0x200, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(4);

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "one", .target_object = 1},
        Elf32DependencyEdge{.requested_name = "two", .target_object = 2},
    };
    graph.objects[1].dependencies = {
        Elf32DependencyEdge{.requested_name = "shared", .target_object = 3},
    };
    graph.objects[2].dependencies = {
        Elf32DependencyEdge{.requested_name = "shared", .target_object = 3},
    };
    graph.objects[3].dependencies = {
        Elf32DependencyEdge{.requested_name = "cycle", .target_object = 0},
    };

    if (!stage_init_array(memory, graph, 3, 0x1010, {0x3001U}) ||
        !stage_init_array(memory, graph, 1, 0x1020, {0U, 0x1001U}) ||
        !stage_init_array(memory, graph, 2, 0x1030,
                          {0xffffffffU, 0x2000U}) ||
        !stage_init_array(memory, graph, 0, 0x1040, {0x4000U})) {
        return fail("could not stage INIT_ARRAY planning graph");
    }

    std::array<std::uint8_t, 0x40> before{};
    if (!memory.read(0x1010, before)) {
        return fail("could not snapshot INIT_ARRAY planning bytes");
    }

    const auto plan = plan_elf32_init_array_calls(
        memory, graph, 0,
        Elf32InitPlanOptions{.max_objects = 4, .max_entries = 6});
    if (!plan || plan.calls.size() != 4) {
        return fail("dependency-first INIT_ARRAY plan did not produce four calls");
    }

    const std::array<std::size_t, 4> expected_objects{3, 1, 2, 0};
    const std::array<std::uint32_t, 4> expected_indices{0, 1, 1, 0};
    const std::array<std::uint32_t, 4> expected_functions{
        0x3001U, 0x1001U, 0x2000U, 0x4000U};
    for (std::size_t index = 0; index < plan.calls.size(); ++index) {
        if (plan.calls[index].object_index != expected_objects[index] ||
            plan.calls[index].array_index != expected_indices[index] ||
            plan.calls[index].function != expected_functions[index]) {
            return fail("INIT_ARRAY plan changed dependency/order/provenance semantics");
        }
    }

    std::array<std::uint8_t, 0x40> after{};
    if (!memory.read(0x1010, after) || after != before ||
        graph.objects[0].dependencies.size() != 2 ||
        graph.objects[3].dependencies.size() != 1) {
        return fail("INIT_ARRAY planning mutated guest memory or graph state");
    }
    return 0;
}

int test_init_execution_arm_thumb_and_side_effects() {
    LinearGuestMemory memory(0x500, 0x1000);

    // ARM constructor: *0x1200 = 42; bx lr.
    constexpr std::array<std::uint8_t, 20> arm_code{
        0x08, 0x00, 0x9F, 0xE5,
        0x2A, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x12, 0x00, 0x00,
    };
    if (!memory.write(0x1100, arm_code)) {
        return fail("could not stage ARM INIT_ARRAY constructor");
    }

    // Thumb constructor: ++*0x1200; bx lr.
    constexpr std::array<std::uint8_t, 16> thumb_code{
        0x02, 0x48,
        0x01, 0x68,
        0x01, 0x31,
        0x01, 0x60,
        0x70, 0x47,
        0x00, 0xBF,
        0x00, 0x12, 0x00, 0x00,
    };
    if (!memory.write(0x1140, thumb_code)) {
        return fail("could not stage Thumb INIT_ARRAY constructor");
    }

    const std::array calls{
        Elf32InitCall{.object_index = 3, .array_index = 0, .function = 0x1100},
        Elf32InitCall{.object_index = 1, .array_index = 2, .function = 0x1141},
    };
    const auto result = execute_elf32_init_calls(
        memory, calls,
        Elf32InitExecutionOptions{
            .stack_top = 0x13f8,
            .return_pc = 0x2000,
            .max_instructions_per_call = 16,
        });
    std::uint32_t value = 0;
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(0x1200, bytes)) {
        return fail("could not read constructor side effect");
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!result || result.calls_completed != 2 || value != 43U) {
        return fail("ARM/Thumb INIT_ARRAY calls did not execute in plan order");
    }
    return 0;
}

int test_init_plan_executes_dependency_first_end_to_end() {
    LinearGuestMemory memory(0x800, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(2);
    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
    };

    // Dependency constructor: *0x1300 = 1; bx lr.
    constexpr std::array<std::uint8_t, 20> dep_code{
        0x08, 0x00, 0x9F, 0xE5,
        0x01, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x13, 0x00, 0x00,
    };
    // Root constructor: ++*0x1300; bx lr.
    constexpr std::array<std::uint8_t, 24> root_code{
        0x0C, 0x00, 0x9F, 0xE5,
        0x00, 0x10, 0x90, 0xE5,
        0x01, 0x10, 0x81, 0xE2,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x13, 0x00, 0x00,
    };
    if (!memory.write(0x1100, dep_code) ||
        !memory.write(0x1140, root_code) ||
        !stage_init_array(memory, graph, 1, 0x1200, {0x1100U}) ||
        !stage_init_array(memory, graph, 0, 0x1210, {0x1140U})) {
        return fail("could not stage end-to-end INIT_ARRAY lifecycle fixture");
    }

    const auto plan = plan_elf32_init_array_calls(
        memory, graph, 0,
        Elf32InitPlanOptions{.max_objects = 2, .max_entries = 2});
    if (!plan || plan.calls.size() != 2 ||
        plan.calls[0].object_index != 1 ||
        plan.calls[1].object_index != 0) {
        return fail("end-to-end INIT_ARRAY plan was not dependency-first");
    }

    const auto executed = execute_elf32_init_calls(
        memory, plan.calls,
        Elf32InitExecutionOptions{
            .stack_top = 0x17f8,
            .return_pc = 0x2000,
            .max_instructions_per_call = 8,
        });
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(0x1300, bytes)) {
        return fail("could not read end-to-end constructor state");
    }
    const std::uint32_t value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!executed || executed.calls_completed != 2 || value != 2U) {
        return fail("planned constructors did not execute dependency-first");
    }
    return 0;
}

int test_init_execution_failures_stop_progress() {
    LinearGuestMemory memory(0x500, 0x1000);

    constexpr std::array<std::uint8_t, 4> return_code{
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 4> loop_code{
        0xFE, 0xFF, 0xFF, 0xEA,
    };
    constexpr std::array<std::uint8_t, 4> svc_code{
        0x00, 0x00, 0x00, 0xEF,
    };
    if (!memory.write(0x1100, return_code) ||
        !memory.write(0x1120, loop_code) ||
        !memory.write(0x1140, svc_code)) {
        return fail("could not stage INIT_ARRAY execution failure code");
    }

    const Elf32InitExecutionOptions valid{
        .stack_top = 0x13f8,
        .return_pc = 0x2000,
        .max_instructions_per_call = 4,
    };

    const std::array one_return{
        Elf32InitCall{.object_index = 0, .array_index = 0, .function = 0x1100},
    };
    const auto bad_options = execute_elf32_init_calls(
        memory, one_return,
        Elf32InitExecutionOptions{
            .stack_top = 0x13fc,
            .return_pc = 0x2000,
            .max_instructions_per_call = 2,
        });
    if (bad_options.error != Elf32InitExecutionError::InvalidOptions ||
        bad_options.calls_completed != 0) {
        return fail("unaligned INIT_ARRAY stack top was not rejected");
    }

    const auto bad_return = execute_elf32_init_calls(
        memory, one_return,
        Elf32InitExecutionOptions{
            .stack_top = 0x13f8,
            .return_pc = 0x2002,
            .max_instructions_per_call = 2,
        });
    if (bad_return.error != Elf32InitExecutionError::InvalidOptions ||
        bad_return.calls_completed != 0) {
        return fail("non-word-aligned INIT_ARRAY return PC was not rejected");
    }

    const std::array bad_function{
        Elf32InitCall{.object_index = 7, .array_index = 4, .function = 0x1102},
    };
    const auto invalid = execute_elf32_init_calls(memory, bad_function, valid);
    if (invalid.error != Elf32InitExecutionError::InvalidFunctionAddress ||
        !invalid.failing_call.has_value() || *invalid.failing_call != 0 ||
        !invalid.failing_object.has_value() || *invalid.failing_object != 7) {
        return fail("misaligned ARM constructor was not rejected with provenance");
    }

    // A completed constructor's guest side effects must remain visible when a
    // later constructor exhausts its instruction budget; the executor is not a
    // rollback transaction.
    constexpr std::array<std::uint8_t, 20> write_code{
        0x08, 0x00, 0x9F, 0xE5,
        0x2A, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x00, 0x12, 0x00, 0x00,
    };
    if (!memory.write(0x1160, write_code)) {
        return fail("could not stage side-effecting constructor");
    }
    const std::array exhausted_calls{
        Elf32InitCall{.object_index = 1, .array_index = 0, .function = 0x1160},
        Elf32InitCall{.object_index = 2, .array_index = 0, .function = 0x1120},
        Elf32InitCall{.object_index = 3, .array_index = 0, .function = 0x1100},
    };
    const auto exhausted =
        execute_elf32_init_calls(memory, exhausted_calls, valid);
    std::array<std::uint8_t, 4> preserved_bytes{};
    if (!memory.read(0x1200, preserved_bytes)) {
        return fail("could not read preserved constructor side effect");
    }
    const std::uint32_t preserved =
        static_cast<std::uint32_t>(preserved_bytes[0]) |
        (static_cast<std::uint32_t>(preserved_bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(preserved_bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(preserved_bytes[3]) << 24U);
    if (exhausted.error !=
            Elf32InitExecutionError::InstructionLimitExceeded ||
        exhausted.calls_completed != 1 ||
        !exhausted.failing_call.has_value() ||
        *exhausted.failing_call != 1 ||
        !exhausted.failing_object.has_value() ||
        *exhausted.failing_object != 2 ||
        !exhausted.cpu_result.has_value() ||
        exhausted.cpu_result->instructions_executed != 4 ||
        exhausted.cpu_result->stop_pc_reached ||
        preserved != 42U) {
        return fail("constructor failure did not preserve completed side effects or stop later calls");
    }

    const std::array exception_call{
        Elf32InitCall{.object_index = 5, .array_index = 0, .function = 0x1140},
    };
    const auto exception =
        execute_elf32_init_calls(memory, exception_call, valid);
    if (exception.error != Elf32InitExecutionError::CpuException ||
        exception.calls_completed != 0 ||
        !exception.cpu_result.has_value() ||
        !exception.cpu_result->exception_raised) {
        return fail("constructor CPU exception was not surfaced");
    }

    const std::array fault_call{
        Elf32InitCall{.object_index = 6, .array_index = 0, .function = 0x2000},
    };
    const auto fault = execute_elf32_init_calls(
        memory, fault_call,
        Elf32InitExecutionOptions{
            .stack_top = 0x13f8,
            .return_pc = 0x3000,
            .max_instructions_per_call = 2,
        });
    if (fault.error != Elf32InitExecutionError::MemoryFault ||
        fault.calls_completed != 0 ||
        !fault.cpu_result.has_value() ||
        !fault.cpu_result->memory_fault) {
        return fail("constructor memory fault was not surfaced");
    }
    return 0;
}

int test_init_plan_input_limits_and_decode_failures() {
    LinearGuestMemory memory(0x200, 0x1000);

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        const auto invalid = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 0, .max_entries = 0});
        if (invalid.error != Elf32InitPlanError::InvalidOptions ||
            !invalid.calls.empty()) {
            return fail("zero INIT_ARRAY object ceiling was not rejected");
        }

        const auto bad_root = plan_elf32_init_array_calls(
            memory, graph, 9,
            Elf32InitPlanOptions{.max_objects = 1, .max_entries = 0});
        if (bad_root.error != Elf32InitPlanError::InvalidRootObject ||
            !bad_root.failing_object.has_value() ||
            *bad_root.failing_object != 9) {
            return fail("invalid INIT_ARRAY root was not classified");
        }

        const auto empty = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 1, .max_entries = 0});
        if (!empty || !empty.calls.empty()) {
            return fail("array-free INIT_ARRAY plan did not allow zero entry budget");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(2);
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "child", .target_object = 1},
        };
        const auto capped = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 1, .max_entries = 0});
        if (capped.error != Elf32InitPlanError::ObjectLimitExceeded ||
            !capped.failing_object.has_value() ||
            *capped.failing_object != 1 || !capped.calls.empty()) {
            return fail("INIT_ARRAY unique-object ceiling was not enforced");
        }

        graph.objects[0].dependencies[0].target_object = 9;
        const auto bad_edge = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 2, .max_entries = 0});
        if (bad_edge.error != Elf32InitPlanError::InvalidGraphEdge ||
            !bad_edge.failing_object.has_value() ||
            *bad_edge.failing_object != 0 || !bad_edge.calls.empty()) {
            return fail("invalid INIT_ARRAY graph edge was not rejected");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        if (!stage_init_array(memory, graph, 0, 0x1080,
                              {0x1111U, 0x2222U})) {
            return fail("could not stage INIT_ARRAY entry-limit fixture");
        }
        const auto capped = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 1, .max_entries = 1});
        if (capped.error != Elf32InitPlanError::EntryLimitExceeded ||
            capped.decode_error !=
                Elf32FunctionArrayDecodeError::TooManyEntries ||
            !capped.failing_object.has_value() ||
            *capped.failing_object != 0 || !capped.calls.empty()) {
            return fail("INIT_ARRAY total-entry ceiling was not surfaced");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        graph.objects[0].linker_metadata.init_array =
            Elf32FunctionArrayMetadata{
                .guest_address = 0x9000,
                .size = 4,
            };
        const auto failed = plan_elf32_init_array_calls(
            memory, graph, 0,
            Elf32InitPlanOptions{.max_objects = 1, .max_entries = 1});
        if (failed.error != Elf32InitPlanError::DecodeFailed ||
            failed.decode_error != Elf32FunctionArrayDecodeError::ReadFailed ||
            !failed.failing_object.has_value() ||
            *failed.failing_object != 0 || !failed.calls.empty()) {
            return fail("INIT_ARRAY nested decoder failure was not surfaced");
        }
    }

    return 0;
}


int test_fini_plan_reverse_order_cycles_and_sentinels() {
    LinearGuestMemory memory(0x200, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(4);

    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "one", .target_object = 1},
        Elf32DependencyEdge{.requested_name = "two", .target_object = 2},
    };
    graph.objects[1].dependencies = {
        Elf32DependencyEdge{.requested_name = "shared", .target_object = 3},
    };
    graph.objects[2].dependencies = {
        Elf32DependencyEdge{.requested_name = "shared", .target_object = 3},
    };
    graph.objects[3].dependencies = {
        Elf32DependencyEdge{.requested_name = "cycle", .target_object = 0},
    };

    if (!stage_fini_array(memory, graph, 3, 0x1090, {0x3001U}) ||
        !stage_fini_array(memory, graph, 1, 0x10a0, {0U, 0x1001U}) ||
        !stage_fini_array(memory, graph, 2, 0x10b0,
                          {0xffffffffU, 0x2000U}) ||
        !stage_fini_array(memory, graph, 0, 0x10c0,
                          {0x4000U, 0x4004U})) {
        return fail("could not stage FINI_ARRAY planning graph");
    }

    const auto plan = plan_elf32_fini_array_calls(
        memory, graph, 0,
        Elf32FiniPlanOptions{.max_objects = 4, .max_entries = 7});
    if (!plan || plan.calls.size() != 5) {
        return fail("reverse FINI_ARRAY plan did not produce five calls");
    }

    const std::array<std::size_t, 5> expected_objects{0, 0, 2, 1, 3};
    const std::array<std::uint32_t, 5> expected_indices{1, 0, 1, 1, 0};
    const std::array<std::uint32_t, 5> expected_functions{
        0x4004U, 0x4000U, 0x2000U, 0x1001U, 0x3001U};
    for (std::size_t index = 0; index < plan.calls.size(); ++index) {
        if (plan.calls[index].object_index != expected_objects[index] ||
            plan.calls[index].array_index != expected_indices[index] ||
            plan.calls[index].function != expected_functions[index]) {
            return fail("FINI_ARRAY plan changed reverse order/provenance semantics");
        }
    }
    return 0;
}

int test_fini_plan_limits_and_decode_failures() {
    LinearGuestMemory memory(0x200, 0x1000);

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        const auto bad_options = plan_elf32_fini_array_calls(
            memory, graph, 0,
            Elf32FiniPlanOptions{.max_objects = 0, .max_entries = 0});
        if (bad_options.error != Elf32FiniPlanError::InvalidOptions ||
            !bad_options.calls.empty()) {
            return fail("zero FINI_ARRAY object ceiling was not rejected");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        graph.objects[0].dependencies = {
            Elf32DependencyEdge{.requested_name = "bad", .target_object = 9},
        };
        const auto bad_edge = plan_elf32_fini_array_calls(
            memory, graph, 0,
            Elf32FiniPlanOptions{.max_objects = 1, .max_entries = 0});
        if (bad_edge.error != Elf32FiniPlanError::InvalidGraphEdge ||
            !bad_edge.failing_object.has_value() ||
            *bad_edge.failing_object != 0 || !bad_edge.calls.empty()) {
            return fail("invalid FINI_ARRAY graph edge was not rejected");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        if (!stage_fini_array(memory, graph, 0, 0x10d0,
                              {0x1111U, 0x2222U})) {
            return fail("could not stage FINI_ARRAY entry-limit fixture");
        }
        const auto capped = plan_elf32_fini_array_calls(
            memory, graph, 0,
            Elf32FiniPlanOptions{.max_objects = 1, .max_entries = 1});
        if (capped.error != Elf32FiniPlanError::EntryLimitExceeded ||
            capped.decode_error !=
                Elf32FunctionArrayDecodeError::TooManyEntries ||
            !capped.failing_object.has_value() ||
            *capped.failing_object != 0 || !capped.calls.empty()) {
            return fail("FINI_ARRAY total-entry ceiling was not surfaced");
        }
    }

    {
        Elf32DependencyGraph graph;
        graph.objects.resize(1);
        graph.objects[0].linker_metadata.fini_array =
            Elf32FunctionArrayMetadata{
                .guest_address = 0x9000,
                .size = 4,
            };
        const auto failed = plan_elf32_fini_array_calls(
            memory, graph, 0,
            Elf32FiniPlanOptions{.max_objects = 1, .max_entries = 1});
        if (failed.error != Elf32FiniPlanError::DecodeFailed ||
            failed.decode_error != Elf32FunctionArrayDecodeError::ReadFailed ||
            !failed.failing_object.has_value() ||
            *failed.failing_object != 0 || !failed.calls.empty()) {
            return fail("FINI_ARRAY nested decoder failure was not surfaced");
        }
    }

    return 0;
}

int test_fini_plan_executes_requester_first_end_to_end() {
    LinearGuestMemory memory(0x800, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(2);
    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
    };

    constexpr std::array<std::uint8_t, 20> root_code{
        0x08, 0x00, 0x9F, 0xE5,
        0x01, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 24> dep_code{
        0x0C, 0x00, 0x9F, 0xE5,
        0x00, 0x10, 0x90, 0xE5,
        0x01, 0x10, 0x81, 0xE2,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    if (!memory.write(0x1100, root_code) ||
        !memory.write(0x1140, dep_code) ||
        !stage_fini_array(memory, graph, 0, 0x1240, {0x1100U}) ||
        !stage_fini_array(memory, graph, 1, 0x1250, {0x1140U})) {
        return fail("could not stage end-to-end FINI_ARRAY lifecycle fixture");
    }

    const auto plan = plan_elf32_fini_array_calls(
        memory, graph, 0,
        Elf32FiniPlanOptions{.max_objects = 2, .max_entries = 2});
    if (!plan || plan.calls.size() != 2 ||
        plan.calls[0].object_index != 0 ||
        plan.calls[1].object_index != 1) {
        return fail("end-to-end FINI_ARRAY plan was not requester-first");
    }

    const auto executed = execute_elf32_fini_calls(
        memory, plan.calls,
        liba32android::elf::Elf32FiniExecutionOptions{
            .stack_top = 0x17f8,
            .return_pc = 0x2000,
            .max_instructions_per_call = 8,
        });
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(0x1380, bytes)) {
        return fail("could not read end-to-end destructor state");
    }
    const std::uint32_t value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!executed || executed.calls_completed != 2 || value != 2U) {
        return fail("planned destructors did not execute requester-first");
    }
    return 0;
}


int test_persistent_legacy_lifecycle_and_once_state() {
    LinearGuestMemory memory(0x1000, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(2);
    graph.objects[0].dependencies = {
        Elf32DependencyEdge{.requested_name = "dep", .target_object = 1},
    };

    constexpr std::array<std::uint8_t, 20> set_one{
        0x08, 0x00, 0x9F, 0xE5,
        0x01, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 24> increment{
        0x0C, 0x00, 0x9F, 0xE5,
        0x00, 0x10, 0x90, 0xE5,
        0x01, 0x10, 0x81, 0xE2,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 20> set_five{
        0x08, 0x00, 0x9F, 0xE5,
        0x05, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 20> set_seven{
        0x08, 0x00, 0x9F, 0xE5,
        0x07, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 20> set_nine{
        0x08, 0x00, 0x9F, 0xE5,
        0x09, 0x10, 0xA0, 0xE3,
        0x00, 0x10, 0x80, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };

    if (!memory.write(0x1100, set_one) ||
        !memory.write(0x1140, increment) ||
        !memory.write(0x1180, set_five) ||
        !memory.write(0x11c0, set_seven) ||
        !memory.write(0x1200, increment) ||
        !memory.write(0x1240, set_nine) ||
        !memory.write(0x1280, increment) ||
        !stage_init_array(memory, graph, 0, 0x1300, {0x1180U}) ||
        !stage_fini_array(memory, graph, 0, 0x1310, {0x11c0U}) ||
        !stage_fini_array(memory, graph, 1, 0x1320, {0x1240U})) {
        return fail("could not stage persistent legacy lifecycle fixture");
    }

    graph.objects[1].linker_metadata.init_function = 0x1100U;
    graph.objects[0].linker_metadata.init_function = 0x1140U;
    graph.objects[0].linker_metadata.fini_function = 0x1200U;
    graph.objects[1].linker_metadata.fini_function = 0x1280U;

    Elf32LifecycleState state;
    const Elf32PersistentLifecycleOptions options{
        .max_objects = 2U,
        .max_array_entries = 3U,
        .execution = Elf32InitExecutionOptions{
            .stack_top = 0x17f8U,
            .return_pc = 0x2000U,
            .max_instructions_per_call = 16U,
        },
    };

    const auto initialized = run_elf32_persistent_constructors(
        memory, graph, state, 0U, options);
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(0x1380U, bytes)) {
        return fail("could not read persistent constructor state");
    }
    const auto value_after_init =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!initialized ||
        initialized.objects_completed != 2U ||
        initialized.calls_completed != 3U ||
        value_after_init != 5U ||
        state.objects.size() != 2U ||
        state.objects[0].constructors != Elf32LifecycleObjectStatus::Complete ||
        state.objects[1].constructors != Elf32LifecycleObjectStatus::Complete) {
        return fail("persistent constructors did not run dependency/DT_INIT/array order");
    }

    const auto repeated_init = run_elf32_persistent_constructors(
        memory, graph, state, 0U, options);
    if (!repeated_init ||
        repeated_init.objects_completed != 0U ||
        repeated_init.calls_completed != 0U) {
        return fail("persistent constructor state did not suppress replay");
    }

    const auto finalized = run_elf32_persistent_destructors(
        memory, graph, state, 0U, options);
    if (!memory.read(0x1380U, bytes)) {
        return fail("could not read persistent destructor state");
    }
    const auto value_after_fini =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!finalized ||
        finalized.objects_completed != 2U ||
        finalized.calls_completed != 4U ||
        value_after_fini != 10U ||
        state.objects[0].destructors != Elf32LifecycleObjectStatus::Complete ||
        state.objects[1].destructors != Elf32LifecycleObjectStatus::Complete) {
        return fail("persistent destructors did not run array/DT_FINI/requester order");
    }

    const auto repeated_fini = run_elf32_persistent_destructors(
        memory, graph, state, 0U, options);
    if (!repeated_fini ||
        repeated_fini.objects_completed != 0U ||
        repeated_fini.calls_completed != 0U) {
        return fail("persistent destructor state did not suppress replay");
    }
    return 0;
}

int test_persistent_lifecycle_failure_latches_state() {
    LinearGuestMemory memory(0x400, 0x1000);
    constexpr std::array<std::uint8_t, 4> loop_code{
        0xFE, 0xFF, 0xFF, 0xEA,
    };
    if (!memory.write(0x1100U, loop_code)) {
        return fail("could not stage persistent lifecycle failure fixture");
    }

    Elf32DependencyGraph graph;
    graph.objects.resize(1);
    graph.objects[0].linker_metadata.init_function = 0x1100U;
    Elf32LifecycleState state;
    const Elf32PersistentLifecycleOptions options{
        .max_objects = 1U,
        .max_array_entries = 0U,
        .execution = Elf32InitExecutionOptions{
            .stack_top = 0x13f8U,
            .return_pc = 0x2000U,
            .max_instructions_per_call = 2U,
        },
    };

    const auto first = run_elf32_persistent_constructors(
        memory, graph, state, 0U, options);
    if (first.error != Elf32PersistentLifecycleError::ExecutionFailed ||
        first.execution_error !=
            Elf32InitExecutionError::InstructionLimitExceeded ||
        state.objects.size() != 1U ||
        state.objects[0].constructors != Elf32LifecycleObjectStatus::Failed) {
        return fail("persistent lifecycle execution failure was not latched");
    }

    const auto repeated = run_elf32_persistent_constructors(
        memory, graph, state, 0U, options);
    if (repeated.error != Elf32PersistentLifecycleError::InvalidState ||
        repeated.calls_completed != 0U) {
        return fail("failed persistent lifecycle object was replayed");
    }
    return 0;
}


class NestedLifecycleContextService final
    : public A32HostServiceHandler {
public:
    explicit NestedLifecycleContextService(
        Elf32LifecycleExecutionContext& context) noexcept
        : context_(context) {}

    A32HostServiceDisposition handle(
        liba32android::memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>&,
        std::uint32_t&) override {
        if (svc_immediate == 0xD5U) {
            inner_seen =
                context_.object_index ==
                std::optional<std::size_t>{5U};
            return inner_seen
                ? A32HostServiceDisposition::Handled
                : A32HostServiceDisposition::Failed;
        }
        if (svc_immediate != 0xD4U) {
            return A32HostServiceDisposition::Unhandled;
        }

        outer_before_nested =
            context_.object_index ==
            std::optional<std::size_t>{3U};
        const std::array<Elf32InitCall, 1> inner_calls{{
            {
                .object_index = 5U,
                .array_index = 0U,
                .function = 0x1140U,
            },
        }};
        const auto inner = execute_elf32_init_calls(
            memory,
            inner_calls,
            Elf32InitExecutionOptions{
                .stack_top = 0x16f8U,
                .return_pc = 0x2000U,
                .max_instructions_per_call = 8U,
                .service_handler = this,
                .max_service_calls_per_call = 1U,
                .execution_context = &context_,
            });
        outer_after_nested =
            context_.object_index ==
            std::optional<std::size_t>{3U};
        return inner && outer_before_nested &&
                       inner_seen && outer_after_nested
            ? A32HostServiceDisposition::Handled
            : A32HostServiceDisposition::Failed;
    }

    bool outer_before_nested{};
    bool inner_seen{};
    bool outer_after_nested{};

private:
    Elf32LifecycleExecutionContext& context_;
};

int test_lifecycle_execution_context_nested_restoration() {
    LinearGuestMemory memory(0x800U, 0x1000U);
    constexpr std::array<std::uint8_t, 8> outer_code{
        0xD4, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    constexpr std::array<std::uint8_t, 8> inner_code{
        0xD5, 0x00, 0x00, 0xEF,
        0x1E, 0xFF, 0x2F, 0xE1,
    };
    if (!memory.write(0x1100U, outer_code) ||
        !memory.write(0x1140U, inner_code)) {
        return fail("could not stage nested lifecycle context fixture");
    }

    Elf32LifecycleExecutionContext context{
        .object_index = 9U,
    };
    NestedLifecycleContextService service{context};
    const std::array<Elf32InitCall, 1> calls{{
        {
            .object_index = 3U,
            .array_index = 0U,
            .function = 0x1100U,
        },
    }};
    const auto executed = execute_elf32_init_calls(
        memory,
        calls,
        Elf32InitExecutionOptions{
            .stack_top = 0x17f8U,
            .return_pc = 0x2000U,
            .max_instructions_per_call = 16U,
            .service_handler = &service,
            .max_service_calls_per_call = 1U,
            .execution_context = &context,
        });
    if (!executed ||
        executed.calls_completed != 1U ||
        !service.outer_before_nested ||
        !service.inner_seen ||
        !service.outer_after_nested ||
        context.object_index != std::optional<std::size_t>{9U}) {
        return fail("lifecycle object context did not restore across nesting");
    }
    return 0;
}

class LifecycleMarkerService final : public A32HostServiceHandler {
public:
    A32HostServiceDisposition handle(
        liba32android::memory::GuestMemory&,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t&) override {
        if (svc_immediate != 0xD3U) {
            return A32HostServiceDisposition::Unhandled;
        }
        ++calls;
        regs[0] = 42U;
        return A32HostServiceDisposition::Handled;
    }

    std::size_t calls{};
};

int test_fini_execution_dispatches_guest_services() {
    LinearGuestMemory memory(0x800, 0x1000);
    Elf32DependencyGraph graph;
    graph.objects.resize(1);

    constexpr std::array<std::uint8_t, 20> fini_code{
        0xD3, 0x00, 0x00, 0xEF,
        0x04, 0x10, 0x9F, 0xE5,
        0x00, 0x00, 0x81, 0xE5,
        0x1E, 0xFF, 0x2F, 0xE1,
        0x80, 0x13, 0x00, 0x00,
    };
    if (!memory.write(0x1100U, fini_code) ||
        !stage_fini_array(memory, graph, 0U, 0x1200U, {0x1100U})) {
        return fail("could not stage service-aware FINI fixture");
    }

    const auto plan = plan_elf32_fini_array_calls(
        memory,
        graph,
        0U,
        Elf32FiniPlanOptions{
            .max_objects = 1U,
            .max_entries = 1U,
        });
    if (!plan || plan.calls.size() != 1U) {
        return fail("service-aware FINI plan was not produced");
    }

    LifecycleMarkerService marker;
    const std::array<A32HostServiceRegistryEntry, 1> entries{{
        {0xD3U, &marker},
    }};
    A32HostServiceRegistry registry{std::span{entries}};

    const auto executed = execute_elf32_fini_calls(
        memory,
        plan.calls,
        Elf32FiniExecutionOptions{
            .stack_top = 0x17f8U,
            .return_pc = 0x2000U,
            .max_instructions_per_call = 16U,
            .service_handler = &registry,
            .max_service_calls_per_call = 1U,
        });
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(0x1380U, bytes)) {
        return fail("could not read service-aware FINI marker");
    }
    const std::uint32_t value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (!executed ||
        executed.calls_completed != 1U ||
        marker.calls != 1U ||
        value != 42U) {
        return fail("FINI execution did not dispatch and resume guest service");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int status = test_exact_decode_and_raw_sentinels(); status != 0) {
        return status;
    }
    if (const int status = test_entry_ceiling_preflights_before_read();
        status != 0) {
        return status;
    }
    if (const int status = test_invalid_size_and_range(); status != 0) {
        return status;
    }
    if (const int status = test_unreadable_and_empty_arrays(); status != 0) {
        return status;
    }
    if (const int status = test_init_plan_dependency_order_cycles_and_sentinels();
        status != 0) {
        return status;
    }
    if (const int status = test_init_plan_input_limits_and_decode_failures();
        status != 0) {
        return status;
    }
    if (const int status = test_init_execution_arm_thumb_and_side_effects();
        status != 0) {
        return status;
    }
    if (const int status = test_init_plan_executes_dependency_first_end_to_end();
        status != 0) {
        return status;
    }
    if (const int status = test_init_execution_failures_stop_progress();
        status != 0) {
        return status;
    }
    if (const int status = test_fini_plan_reverse_order_cycles_and_sentinels();
        status != 0) {
        return status;
    }
    if (const int status = test_fini_plan_limits_and_decode_failures();
        status != 0) {
        return status;
    }
    if (const int status = test_fini_plan_executes_requester_first_end_to_end();
        status != 0) {
        return status;
    }
    if (const int status = test_fini_execution_dispatches_guest_services();
        status != 0) {
        return status;
    }
    if (const int status =
            test_lifecycle_execution_context_nested_restoration();
        status != 0) {
        return status;
    }
    if (const int status = test_persistent_legacy_lifecycle_and_once_state();
        status != 0) {
        return status;
    }
    if (const int status = test_persistent_lifecycle_failure_latches_state();
        status != 0) {
        return status;
    }
    return 0;
}
