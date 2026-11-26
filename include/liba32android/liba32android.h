#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define LIBA32ANDROID_API __attribute__((visibility("default")))
#else
#define LIBA32ANDROID_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LIBA32ANDROID_API_VERSION 1u

typedef struct liba32android_runtime liba32android_runtime;

typedef enum liba32android_status {
    LIBA32ANDROID_STATUS_OK = 0,
    LIBA32ANDROID_STATUS_INVALID_ARGUMENT = 1,
    LIBA32ANDROID_STATUS_ALLOCATION_FAILED = 2,
    LIBA32ANDROID_STATUS_MEMORY_ERROR = 3,
    LIBA32ANDROID_STATUS_EXECUTION_ERROR = 4,
    LIBA32ANDROID_STATUS_INTERNAL_ERROR = 5
} liba32android_status;

typedef enum liba32android_instruction_set {
    LIBA32ANDROID_INSTRUCTION_SET_ARM = 0,
    LIBA32ANDROID_INSTRUCTION_SET_THUMB = 1
} liba32android_instruction_set;

enum {
    LIBA32ANDROID_MEMORY_READ = 1u << 0,
    LIBA32ANDROID_MEMORY_WRITE = 1u << 1,
    LIBA32ANDROID_MEMORY_EXECUTE = 1u << 2
};

enum {
    LIBA32ANDROID_EXEC_REQUEST_HAS_STOP_PC = 1u << 0,
    LIBA32ANDROID_EXEC_REQUEST_HAS_INITIAL_CPSR = 1u << 1
};

enum {
    LIBA32ANDROID_EXEC_RESULT_STOP_PC_REACHED = 1u << 0,
    LIBA32ANDROID_EXEC_RESULT_SVC_TRAP = 1u << 1,
    LIBA32ANDROID_EXEC_RESULT_FASTMEM_ENABLED = 1u << 2,
    LIBA32ANDROID_EXEC_RESULT_MEMORY_FAULT = 1u << 3,
    LIBA32ANDROID_EXEC_RESULT_EXCEPTION = 1u << 4
};

// On every API call that accepts this structure, required receives the complete
// error-text byte count excluding the NUL terminator. If data/capacity are
// supplied, the message is always NUL-terminated and may be truncated.
// Success clears required and writes an empty string when possible.
typedef struct liba32android_error_buffer {
    char* data;
    size_t capacity;
    size_t required;
} liba32android_error_buffer;

// Callers set struct_size to sizeof(liba32android_execution_request). Future
// API versions may append fields; version 1 accepts current-or-larger structs
// and reads only the known prefix.
typedef struct liba32android_execution_request {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t instruction_set;
    uint32_t entry_pc;
    uint32_t regs[16];
    uint64_t instruction_count;
    uint32_t stop_pc;
    uint32_t initial_cpsr;
} liba32android_execution_request;

// Callers set struct_size to sizeof(liba32android_execution_result) before
// execution. Version 1 accepts current-or-larger storage and writes only the
// known prefix while preserving the caller-provided struct_size value.
typedef struct liba32android_execution_result {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t regs[16];
    uint32_t cpsr;
    uint32_t svc_immediate;
    uint64_t instructions_executed;
} liba32android_execution_result;

LIBA32ANDROID_API uint32_t liba32android_api_version(void);
LIBA32ANDROID_API const char* liba32android_status_name(
    liba32android_status status);

LIBA32ANDROID_API liba32android_status liba32android_runtime_create(
    liba32android_runtime** out_runtime,
    liba32android_error_buffer* error);

LIBA32ANDROID_API void liba32android_runtime_destroy(
    liba32android_runtime* runtime);

LIBA32ANDROID_API liba32android_status liba32android_runtime_page_size(
    const liba32android_runtime* runtime,
    size_t* out_page_size,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_map(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    uint32_t permissions,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_protect(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    uint32_t permissions,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_unmap(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    size_t length,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_read(
    const liba32android_runtime* runtime,
    uint32_t guest_address,
    void* output,
    size_t length,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_write(
    liba32android_runtime* runtime,
    uint32_t guest_address,
    const void* input,
    size_t length,
    liba32android_error_buffer* error);

LIBA32ANDROID_API liba32android_status liba32android_runtime_execute(
    liba32android_runtime* runtime,
    const liba32android_execution_request* request,
    liba32android_execution_result* result,
    liba32android_error_buffer* error);

#ifdef __cplusplus
}
#endif
