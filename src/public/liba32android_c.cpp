#include "liba32android/liba32android.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

struct liba32android_runtime {
    liba32android::memory::MappedGuestMemory memory;
    liba32android::cpu::A32Executor cpu;

    liba32android_runtime() : memory{}, cpu{memory} {}
};

namespace {

using liba32android::memory::MemoryPermission;

void clear_error(liba32android_error_buffer* error) noexcept {
    if (error == nullptr) {
        return;
    }
    error->required = 0U;
    if (error->data != nullptr && error->capacity != 0U) {
        error->data[0] = '\0';
    }
}

void write_error(
    liba32android_error_buffer* error,
    std::string_view text) noexcept {
    if (error == nullptr) {
        return;
    }
    error->required = text.size();
    if (error->data == nullptr || error->capacity == 0U) {
        return;
    }
    const std::size_t copy_size =
        std::min(text.size(), error->capacity - 1U);
    if (copy_size != 0U) {
        std::memcpy(error->data, text.data(), copy_size);
    }
    error->data[copy_size] = '\0';
}

liba32android_status fail(
    liba32android_status status,
    liba32android_error_buffer* error,
    const char* component,
    const char* code,
    const char* message,
    std::optional<std::uint32_t> pc = std::nullopt,
    std::optional<std::uint32_t> address = std::nullopt) noexcept {
    std::array<char, 640> text{};
    if (pc.has_value() && address.has_value()) {
        std::snprintf(
            text.data(),
            text.size(),
            "A32ERR|component=%s|code=%s|pc=0x%08x|addr=0x%08x|message=%s",
            component,
            code,
            static_cast<unsigned>(*pc),
            static_cast<unsigned>(*address),
            message);
    } else if (pc.has_value()) {
        std::snprintf(
            text.data(),
            text.size(),
            "A32ERR|component=%s|code=%s|pc=0x%08x|message=%s",
            component,
            code,
            static_cast<unsigned>(*pc),
            message);
    } else if (address.has_value()) {
        std::snprintf(
            text.data(),
            text.size(),
            "A32ERR|component=%s|code=%s|addr=0x%08x|message=%s",
            component,
            code,
            static_cast<unsigned>(*address),
            message);
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "A32ERR|component=%s|code=%s|message=%s",
            component,
            code,
            message);
    }
    write_error(error, text.data());
    return status;
}

[[nodiscard]] bool valid_guest_range(
    std::uint32_t address,
    std::size_t length) noexcept {
    constexpr std::uint64_t kGuestAddressSpace = 1ULL << 32U;
    return static_cast<std::uint64_t>(length) <=
           kGuestAddressSpace - static_cast<std::uint64_t>(address);
}

[[nodiscard]] bool valid_page_range(
    const liba32android_runtime* runtime,
    std::uint32_t address,
    std::size_t length) noexcept {
    if (runtime == nullptr || length == 0U) {
        return false;
    }
    const std::size_t page_size = runtime->memory.page_size();
    return page_size != 0U &&
           address % page_size == 0U &&
           length % page_size == 0U &&
           valid_guest_range(address, length);
}

bool valid_permission_bits(std::uint32_t permissions) noexcept {
    constexpr std::uint32_t known =
        LIBA32ANDROID_MEMORY_READ |
        LIBA32ANDROID_MEMORY_WRITE |
        LIBA32ANDROID_MEMORY_EXECUTE;
    if ((permissions & ~known) != 0U) {
        return false;
    }
    if ((permissions &
         (LIBA32ANDROID_MEMORY_WRITE |
          LIBA32ANDROID_MEMORY_EXECUTE)) != 0U &&
        (permissions & LIBA32ANDROID_MEMORY_READ) == 0U) {
        return false;
    }
    return true;
}

MemoryPermission to_permissions(std::uint32_t permissions) noexcept {
    MemoryPermission result = MemoryPermission::None;
    if ((permissions & LIBA32ANDROID_MEMORY_READ) != 0U) {
        result = result | MemoryPermission::Read;
    }
    if ((permissions & LIBA32ANDROID_MEMORY_WRITE) != 0U) {
        result = result | MemoryPermission::Write;
    }
    if ((permissions & LIBA32ANDROID_MEMORY_EXECUTE) != 0U) {
        result = result | MemoryPermission::Execute;
    }
    return result;
}

bool valid_runtime(
    const liba32android_runtime* runtime,
    liba32android_error_buffer* error) {
    if (runtime != nullptr) {
        return true;
    }
    static_cast<void>(fail(
        LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
        error,
        "api",
        "NULL_RUNTIME",
        "runtime is null"));
    return false;
}

void fill_execution_result(
    const liba32android::cpu::ExecutionResult& execution,
    liba32android_execution_result* result) {
    const std::uint32_t caller_size = result->struct_size;
    std::memset(result, 0, sizeof(*result));
    result->struct_size = caller_size;
    for (std::size_t i = 0; i < execution.regs.size(); ++i) {
        result->regs[i] = execution.regs[i];
    }
    result->cpsr = execution.cpsr;
    result->instructions_executed =
        static_cast<std::uint64_t>(execution.instructions_executed);
    if (execution.stop_pc_reached) {
        result->flags |= LIBA32ANDROID_EXEC_RESULT_STOP_PC_REACHED;
    }
    if (execution.svc_immediate.has_value()) {
        result->flags |= LIBA32ANDROID_EXEC_RESULT_SVC_TRAP;
        result->svc_immediate = *execution.svc_immediate;
    }
    if (execution.fastmem_enabled) {
        result->flags |= LIBA32ANDROID_EXEC_RESULT_FASTMEM_ENABLED;
    }
    if (execution.memory_fault) {
        result->flags |= LIBA32ANDROID_EXEC_RESULT_MEMORY_FAULT;
    }
    if (execution.exception_raised &&
        !execution.svc_immediate.has_value()) {
        result->flags |= LIBA32ANDROID_EXEC_RESULT_EXCEPTION;
    }
}

}  // namespace

extern "C" {

uint32_t liba32android_api_version(void) {
    return LIBA32ANDROID_API_VERSION;
}

const char* liba32android_status_name(liba32android_status status) {
    switch (status) {
    case LIBA32ANDROID_STATUS_OK:
        return "OK";
    case LIBA32ANDROID_STATUS_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case LIBA32ANDROID_STATUS_ALLOCATION_FAILED:
        return "ALLOCATION_FAILED";
    case LIBA32ANDROID_STATUS_MEMORY_ERROR:
        return "MEMORY_ERROR";
    case LIBA32ANDROID_STATUS_EXECUTION_ERROR:
        return "EXECUTION_ERROR";
    case LIBA32ANDROID_STATUS_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    }
    return "UNKNOWN_STATUS";
}

liba32android_status liba32android_runtime_create(
    liba32android_runtime** out_runtime,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (out_runtime == nullptr) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "NULL_OUTPUT",
            "out_runtime is null");
    }
    *out_runtime = nullptr;
    try {
        *out_runtime = new liba32android_runtime{};
        return LIBA32ANDROID_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return fail(
            LIBA32ANDROID_STATUS_ALLOCATION_FAILED,
            error,
            "runtime",
            "RUNTIME_CREATE_FAILED",
            "could not allocate runtime state");
    } catch (const std::exception&) {
        return fail(
            LIBA32ANDROID_STATUS_ALLOCATION_FAILED,
            error,
            "runtime",
            "RUNTIME_CREATE_FAILED",
            "could not reserve guest runtime");
    } catch (...) {
        return fail(
            LIBA32ANDROID_STATUS_INTERNAL_ERROR,
            error,
            "runtime",
            "RUNTIME_CREATE_FAILED",
            "unexpected runtime creation failure");
    }
}

void liba32android_runtime_destroy(liba32android_runtime* runtime) {
    delete runtime;
}

liba32android_status liba32android_runtime_page_size(
    const liba32android_runtime* runtime,
    size_t* out_page_size,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (out_page_size == nullptr) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "NULL_OUTPUT",
            "out_page_size is null");
    }
    *out_page_size = runtime->memory.page_size();
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_map(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    uint32_t permissions,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_page_range(runtime, guest_address, length) ||
        !valid_permission_bits(permissions)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "memory",
            "INVALID_MAP_ARGUMENT",
            "invalid map length or permissions",
            std::nullopt,
            guest_address);
    }
    if (!runtime->memory.map(
            guest_address, length, to_permissions(permissions))) {
        return fail(
            LIBA32ANDROID_STATUS_MEMORY_ERROR,
            error,
            "memory",
            "GUEST_MAP_FAILED",
            "guest map failed",
            std::nullopt,
            guest_address);
    }
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_protect(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    uint32_t permissions,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_page_range(runtime, guest_address, length) ||
        !valid_permission_bits(permissions)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "memory",
            "INVALID_PROTECT_ARGUMENT",
            "invalid protect length or permissions",
            std::nullopt,
            guest_address);
    }
    if (!runtime->memory.protect(
            guest_address, length, to_permissions(permissions))) {
        return fail(
            LIBA32ANDROID_STATUS_MEMORY_ERROR,
            error,
            "memory",
            "GUEST_PROTECT_FAILED",
            "guest protect failed",
            std::nullopt,
            guest_address);
    }
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_unmap(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_page_range(runtime, guest_address, length)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "memory",
            "INVALID_UNMAP_ARGUMENT",
            "unmap range is not a valid page-aligned guest range",
            std::nullopt,
            guest_address);
    }
    if (!runtime->memory.unmap(guest_address, length)) {
        return fail(
            LIBA32ANDROID_STATUS_MEMORY_ERROR,
            error,
            "memory",
            "GUEST_UNMAP_FAILED",
            "guest unmap failed",
            std::nullopt,
            guest_address);
    }
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_read(
    const liba32android_runtime* runtime,
    uint32_t guest_address,
    void* output,
    size_t length,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (length != 0U && output == nullptr) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "NULL_OUTPUT",
            "read output is null",
            std::nullopt,
            guest_address);
    }
    if (!valid_guest_range(guest_address, length)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "memory",
            "INVALID_GUEST_RANGE",
            "read range exceeds the logical guest address space",
            std::nullopt,
            guest_address);
    }
    std::span<std::uint8_t> bytes{
        static_cast<std::uint8_t*>(output), length};
    if (!runtime->memory.read(guest_address, bytes)) {
        return fail(
            LIBA32ANDROID_STATUS_MEMORY_ERROR,
            error,
            "memory",
            "GUEST_READ_FAILED",
            "guest read failed",
            std::nullopt,
            guest_address);
    }
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_write(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    const void* input,
    size_t length,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (length != 0U && input == nullptr) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "NULL_INPUT",
            "write input is null",
            std::nullopt,
            guest_address);
    }
    if (!valid_guest_range(guest_address, length)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "memory",
            "INVALID_GUEST_RANGE",
            "write range exceeds the logical guest address space",
            std::nullopt,
            guest_address);
    }
    std::span<const std::uint8_t> bytes{
        static_cast<const std::uint8_t*>(input), length};
    if (!runtime->memory.write(guest_address, bytes)) {
        return fail(
            LIBA32ANDROID_STATUS_MEMORY_ERROR,
            error,
            "memory",
            "GUEST_WRITE_FAILED",
            "guest write failed",
            std::nullopt,
            guest_address);
    }
    return LIBA32ANDROID_STATUS_OK;
}

liba32android_status liba32android_runtime_execute(
    liba32android_runtime* runtime,
    const liba32android_execution_request* request,
    liba32android_execution_result* result,
    liba32android_error_buffer* error) {
    clear_error(error);
    if (!valid_runtime(runtime, error)) {
        return LIBA32ANDROID_STATUS_INVALID_ARGUMENT;
    }
    if (request == nullptr || result == nullptr) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "NULL_EXECUTION_ARGUMENT",
            "execution request/result is null");
    }
    if (request->struct_size < sizeof(*request) ||
        result->struct_size < sizeof(*result)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "STRUCT_SIZE_MISMATCH",
            "execution request/result structure is too small");
    }

    constexpr std::uint32_t known_request_flags =
        LIBA32ANDROID_EXEC_REQUEST_HAS_STOP_PC |
        LIBA32ANDROID_EXEC_REQUEST_HAS_INITIAL_CPSR;
    if ((request->flags & ~known_request_flags) != 0U ||
        request->instruction_count == 0U ||
        request->instruction_count >
            std::numeric_limits<std::size_t>::max() ||
        (request->instruction_set !=
             LIBA32ANDROID_INSTRUCTION_SET_ARM &&
         request->instruction_set !=
             LIBA32ANDROID_INSTRUCTION_SET_THUMB)) {
        return fail(
            LIBA32ANDROID_STATUS_INVALID_ARGUMENT,
            error,
            "api",
            "INVALID_EXECUTION_REQUEST",
            "invalid execution flags, ISA, or instruction budget");
    }

    liba32android::cpu::ExecutionRequest internal{};
    internal.instruction_set =
        request->instruction_set == LIBA32ANDROID_INSTRUCTION_SET_THUMB
            ? liba32android::cpu::InstructionSet::Thumb
            : liba32android::cpu::InstructionSet::Arm;
    internal.entry_pc = request->entry_pc;
    for (std::size_t i = 0; i < internal.regs.size(); ++i) {
        internal.regs[i] = request->regs[i];
    }
    internal.instruction_count =
        static_cast<std::size_t>(request->instruction_count);
    if ((request->flags &
         LIBA32ANDROID_EXEC_REQUEST_HAS_STOP_PC) != 0U) {
        internal.stop_pc = request->stop_pc;
    }
    if ((request->flags &
         LIBA32ANDROID_EXEC_REQUEST_HAS_INITIAL_CPSR) != 0U) {
        internal.initial_cpsr = request->initial_cpsr;
    }

    try {
        const auto execution = runtime->cpu.execute(internal);
        fill_execution_result(execution, result);

        if (execution.memory_fault) {
            return fail(
                LIBA32ANDROID_STATUS_MEMORY_ERROR,
                error,
                "memory",
                "GUEST_MEMORY_FAULT",
                "guest execution memory fault",
                execution.regs[15]);
        }
        if (execution.exception_raised &&
            !execution.svc_immediate.has_value()) {
            return fail(
                LIBA32ANDROID_STATUS_EXECUTION_ERROR,
                error,
                "runtime",
                "EXECUTION_EXCEPTION",
                "guest execution raised an exception",
                execution.regs[15]);
        }
        return LIBA32ANDROID_STATUS_OK;
    } catch (const std::exception&) {
        return fail(
            LIBA32ANDROID_STATUS_INTERNAL_ERROR,
            error,
            "runtime",
            "EXECUTION_INTERNAL_FAILURE",
            "internal execution failure",
            request->entry_pc);
    } catch (...) {
        return fail(
            LIBA32ANDROID_STATUS_INTERNAL_ERROR,
            error,
            "runtime",
            "EXECUTION_INTERNAL_FAILURE",
            "unexpected execution failure",
            request->entry_pc);
    }
}

}  // extern "C"
