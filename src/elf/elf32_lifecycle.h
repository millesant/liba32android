#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "cpu/a32_cpu.h"
#include "elf/elf32_dependency_graph.h"
#include "elf/elf32_linker_metadata.h"
#include "memory/guest_memory.h"

namespace liba32android::elf {

struct Elf32FunctionArrayDecodeOptions {
    std::uint32_t max_entries{};
};

enum class Elf32FunctionArrayDecodeError : std::uint8_t {
    None = 0,
    InvalidArraySize,
    RangeOverflow,
    TooManyEntries,
    ReadFailed,
};

struct Elf32FunctionArrayDecodeResult {
    Elf32FunctionArrayDecodeError error{Elf32FunctionArrayDecodeError::None};
    std::vector<std::uint32_t> entries;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32FunctionArrayDecodeError::None;
    }
};

// Decode one validated ELF32 INIT_ARRAY/FINI_ARRAY descriptor without guest
// mutation. Entries are returned as raw logical 32-bit values in declaration
// order. Sentinel filtering and function execution are lifecycle-policy work
// outside this primitive.
[[nodiscard]] Elf32FunctionArrayDecodeResult decode_elf32_function_array(
    const memory::GuestMemory& memory,
    const Elf32FunctionArrayMetadata& array,
    const Elf32FunctionArrayDecodeOptions& options);

[[nodiscard]] const char* to_string(
    Elf32FunctionArrayDecodeError error) noexcept;

struct Elf32InitCall {
    std::size_t object_index{};
    std::uint32_t array_index{};
    std::uint32_t function{};
};

struct Elf32InitPlanOptions {
    std::uint32_t max_objects{};
    // Total raw INIT_ARRAY entries decoded across all visited objects.
    // Sentinel entries consume this budget even though they are not calls.
    std::uint32_t max_entries{};
};

enum class Elf32InitPlanError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidRootObject,
    InvalidGraphEdge,
    ObjectLimitExceeded,
    EntryLimitExceeded,
    DecodeFailed,
};

struct Elf32InitPlanResult {
    Elf32InitPlanError error{Elf32InitPlanError::None};
    Elf32FunctionArrayDecodeError decode_error{
        Elf32FunctionArrayDecodeError::None};
    std::optional<std::size_t> failing_object;
    std::vector<Elf32InitCall> calls;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32InitPlanError::None;
    }
};

// Build one root-scoped constructor plan without guest execution. Reachable
// dependency objects contribute before their requester in stored edge order.
// Cycles/shared dependencies contribute each object at most once. Raw null and
// all-ones array entries are counted against max_entries but omitted as calls.
[[nodiscard]] Elf32InitPlanResult plan_elf32_init_array_calls(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t root_object,
    const Elf32InitPlanOptions& options);

[[nodiscard]] const char* to_string(Elf32InitPlanError error) noexcept;

using Elf32FiniCall = Elf32InitCall;

struct Elf32FiniPlanOptions {
    std::uint32_t max_objects{};
    // Total raw FINI_ARRAY entries decoded across all visited objects.
    // Sentinel entries consume this budget even though they are not calls.
    std::uint32_t max_entries{};
};

enum class Elf32FiniPlanError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidRootObject,
    InvalidGraphEdge,
    ObjectLimitExceeded,
    EntryLimitExceeded,
    DecodeFailed,
};

struct Elf32FiniPlanResult {
    Elf32FiniPlanError error{Elf32FiniPlanError::None};
    Elf32FunctionArrayDecodeError decode_error{
        Elf32FunctionArrayDecodeError::None};
    std::optional<std::size_t> failing_object;
    std::vector<Elf32FiniCall> calls;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32FiniPlanError::None;
    }
};

// Build one root-scoped destructor plan without guest execution. The object
// order is the exact reverse of dependency-first constructor traversal and
// each object's FINI_ARRAY entries are emitted in reverse declaration order.
// Cycles/shared dependencies contribute each object at most once. Raw null and
// all-ones entries count against max_entries but are omitted as calls.
[[nodiscard]] Elf32FiniPlanResult plan_elf32_fini_array_calls(
    const memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    std::size_t root_object,
    const Elf32FiniPlanOptions& options);

[[nodiscard]] const char* to_string(Elf32FiniPlanError error) noexcept;

struct Elf32InitExecutionOptions {
    // Caller-owned writable stack top. The executor does not map or unmap it.
    std::uint32_t stack_top{};
    // Word-aligned normalized logical guest PC used only as a stop target; it
    // need not be mapped because CPU execution stops before fetching it. Word
    // alignment lets the same target safely serve ARM and Thumb constructors.
    std::uint32_t return_pc{};
    std::size_t max_instructions_per_call{};
};

enum class Elf32InitExecutionError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidFunctionAddress,
    CpuException,
    MemoryFault,
    InstructionLimitExceeded,
};

struct Elf32InitExecutionResult {
    Elf32InitExecutionError error{Elf32InitExecutionError::None};
    std::size_t calls_completed{};
    std::optional<std::size_t> failing_call;
    std::optional<std::size_t> failing_object;
    std::optional<cpu::ExecutionResult> cpu_result;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32InitExecutionError::None;
    }
};

// Execute a precomputed INIT_ARRAY call plan in order. Each call starts with
// deterministic zeroed registers except SP/LR, derives ARM/Thumb from function
// bit 0, and must return to return_pc within the per-call instruction ceiling.
// Completed constructor side effects are intentionally not rolled back.
[[nodiscard]] Elf32InitExecutionResult execute_elf32_init_calls(
    memory::GuestMemory& memory,
    std::span<const Elf32InitCall> calls,
    const Elf32InitExecutionOptions& options);

[[nodiscard]] const char* to_string(
    Elf32InitExecutionError error) noexcept;

using Elf32FiniExecutionOptions = Elf32InitExecutionOptions;
using Elf32FiniExecutionError = Elf32InitExecutionError;
using Elf32FiniExecutionResult = Elf32InitExecutionResult;

// FINI_ARRAY calls use the same bounded A32 call seam as constructors; only
// planning order differs.
[[nodiscard]] Elf32FiniExecutionResult execute_elf32_fini_calls(
    memory::GuestMemory& memory,
    std::span<const Elf32FiniCall> calls,
    const Elf32FiniExecutionOptions& options);

enum class Elf32LifecycleObjectStatus : std::uint8_t {
    Pending = 0,
    Complete,
    Failed,
};

struct Elf32LifecycleObjectState {
    Elf32LifecycleObjectStatus constructors{Elf32LifecycleObjectStatus::Pending};
    Elf32LifecycleObjectStatus destructors{Elf32LifecycleObjectStatus::Pending};
};

// Caller-owned persistent state keyed by stable dependency-graph object index.
// The vector may grow when a persistent link map appends new objects, but it
// may never be larger than the graph presented to a lifecycle operation.
struct Elf32LifecycleState {
    std::vector<Elf32LifecycleObjectState> objects;
};

struct Elf32PersistentLifecycleOptions {
    std::uint32_t max_objects{};
    // Total raw array entries decoded by this invocation. Legacy DT_INIT/DT_FINI
    // calls do not consume this array-entry ceiling.
    std::uint32_t max_array_entries{};
    Elf32InitExecutionOptions execution;
};

enum class Elf32PersistentLifecycleError : std::uint8_t {
    None = 0,
    InvalidOptions,
    InvalidRootObject,
    InvalidState,
    InvalidGraphEdge,
    ObjectLimitExceeded,
    EntryLimitExceeded,
    DecodeFailed,
    ExecutionFailed,
};

struct Elf32PersistentLifecycleResult {
    Elf32PersistentLifecycleError error{
        Elf32PersistentLifecycleError::None};
    Elf32FunctionArrayDecodeError decode_error{
        Elf32FunctionArrayDecodeError::None};
    Elf32InitExecutionError execution_error{
        Elf32InitExecutionError::None};
    std::size_t objects_completed{};
    std::size_t calls_completed{};
    std::optional<std::size_t> failing_object;
    std::optional<cpu::ExecutionResult> cpu_result;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Elf32PersistentLifecycleError::None;
    }
};

// Execute constructors at most once per persistent object state. Dependencies
// are initialized before requesters; for each object DT_INIT precedes
// INIT_ARRAY. A failed guest call latches that object's constructor state as
// Failed so later invocations cannot replay partial side effects.
[[nodiscard]] Elf32PersistentLifecycleResult
run_elf32_persistent_constructors(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    Elf32LifecycleState& state,
    std::size_t root_object,
    const Elf32PersistentLifecycleOptions& options);

// Execute destructors at most once per initialized object. Requesters are
// finalized before dependencies; each object runs FINI_ARRAY in reverse order
// before DT_FINI. A failed guest call latches the object's destructor state.
[[nodiscard]] Elf32PersistentLifecycleResult
run_elf32_persistent_destructors(
    memory::GuestMemory& memory,
    const Elf32DependencyGraph& graph,
    Elf32LifecycleState& state,
    std::size_t root_object,
    const Elf32PersistentLifecycleOptions& options);

[[nodiscard]] const char* to_string(
    Elf32PersistentLifecycleError error) noexcept;

}  // namespace liba32android::elf
