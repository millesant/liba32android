#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <liba32android/liba32android.h>

static int fail(const char* message, const char* error_text) {
    fprintf(stderr, "%s", message);
    if (error_text != NULL && error_text[0] != '\0') {
        fprintf(stderr, ": %s", error_text);
    }
    fputc('\n', stderr);
    return 1;
}

int main(void) {
    char error_storage[512] = {0};
    liba32android_error_buffer error = {
        .data = error_storage,
        .capacity = sizeof(error_storage),
        .required = 0,
    };

    if (liba32android_api_version() != LIBA32ANDROID_API_VERSION ||
        strcmp(liba32android_status_name(LIBA32ANDROID_STATUS_OK), "OK") != 0) {
        return fail("public API version/status contract failed", NULL);
    }

    liba32android_runtime* runtime = NULL;
    if (liba32android_runtime_create(&runtime, &error) !=
            LIBA32ANDROID_STATUS_OK ||
        runtime == NULL ||
        error.required != 0) {
        return fail("public runtime creation failed", error_storage);
    }

    size_t page_size = 0;
    if (liba32android_runtime_page_size(
            runtime, &page_size, &error) != LIBA32ANDROID_STATUS_OK ||
        page_size == 0) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime page-size query failed", error_storage);
    }

    const uint32_t code_address = 0x00010000u;
    const uint32_t stop_address = 0x00020000u;

    if (liba32android_runtime_map(
            runtime,
            code_address + 1u,
            page_size,
            LIBA32ANDROID_MEMORY_READ,
            &error) != LIBA32ANDROID_STATUS_INVALID_ARGUMENT ||
        strstr(error_storage, "code=INVALID_MAP_ARGUMENT") == NULL) {
        liba32android_runtime_destroy(runtime);
        return fail("public invalid map range classification failed", error_storage);
    }

    uint8_t invalid_range_byte = 0;
    if (liba32android_runtime_read(
            runtime,
            0xffffffffu,
            &invalid_range_byte,
            2u,
            &error) != LIBA32ANDROID_STATUS_INVALID_ARGUMENT ||
        strstr(error_storage, "code=INVALID_GUEST_RANGE") == NULL) {
        liba32android_runtime_destroy(runtime);
        return fail("public overflowing read range classification failed", error_storage);
    }
    if (liba32android_runtime_map(
            runtime,
            code_address,
            page_size,
            LIBA32ANDROID_MEMORY_READ | LIBA32ANDROID_MEMORY_WRITE,
            &error) != LIBA32ANDROID_STATUS_OK) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime code map failed", error_storage);
    }

    const uint8_t code[] = {
        0x2a, 0x00, 0xa0, 0xe3, /* mov r0, #42 */
        0x1e, 0xff, 0x2f, 0xe1, /* bx lr */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x00, 0x00, 0xef, /* svc #0x12 */
    };
    if (liba32android_runtime_write(
            runtime,
            code_address,
            code,
            sizeof(code),
            &error) != LIBA32ANDROID_STATUS_OK ||
        liba32android_runtime_protect(
            runtime,
            code_address,
            page_size,
            LIBA32ANDROID_MEMORY_READ | LIBA32ANDROID_MEMORY_EXECUTE,
            &error) != LIBA32ANDROID_STATUS_OK) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime code staging failed", error_storage);
    }

    liba32android_execution_request request = {0};
    request.struct_size = sizeof(request);
    request.flags = LIBA32ANDROID_EXEC_REQUEST_HAS_STOP_PC;
    request.instruction_set = LIBA32ANDROID_INSTRUCTION_SET_ARM;
    request.entry_pc = code_address;
    request.regs[14] = stop_address;
    request.instruction_count = 16;
    request.stop_pc = stop_address;

    liba32android_execution_result result = {0};
    result.struct_size = sizeof(result);
    if (liba32android_runtime_execute(
            runtime, &request, &result, &error) !=
            LIBA32ANDROID_STATUS_OK ||
        (result.flags & LIBA32ANDROID_EXEC_RESULT_STOP_PC_REACHED) == 0 ||
        result.regs[0] != 42u ||
        error.required != 0) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime ARM execution failed", error_storage);
    }

    request = (liba32android_execution_request){0};
    request.struct_size = sizeof(request);
    request.instruction_set = LIBA32ANDROID_INSTRUCTION_SET_ARM;
    request.entry_pc = code_address + 16u;
    request.instruction_count = 1;
    result = (liba32android_execution_result){0};
    result.struct_size = sizeof(result);
    if (liba32android_runtime_execute(
            runtime, &request, &result, &error) !=
            LIBA32ANDROID_STATUS_OK ||
        (result.flags & LIBA32ANDROID_EXEC_RESULT_SVC_TRAP) == 0 ||
        result.svc_immediate != 0x12u) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime SVC trap exposure failed", error_storage);
    }

    uint8_t byte = 0;
    if (liba32android_runtime_read(
            runtime, 0xdeadb000u, &byte, 1u, &error) !=
            LIBA32ANDROID_STATUS_MEMORY_ERROR ||
        strstr(error_storage, "A32ERR|component=memory|code=GUEST_READ_FAILED") == NULL ||
        strstr(error_storage, "addr=0xdeadb000") == NULL ||
        error.required == 0) {
        liba32android_runtime_destroy(runtime);
        return fail("public structured memory error failed", error_storage);
    }

    if (liba32android_runtime_read(
            runtime, code_address, &byte, 1u, &error) !=
            LIBA32ANDROID_STATUS_OK ||
        error.required != 0 ||
        error_storage[0] != '\0') {
        liba32android_runtime_destroy(runtime);
        return fail("public error buffer was not cleared on success", error_storage);
    }

    if (liba32android_runtime_unmap(
            runtime, code_address, page_size, &error) !=
        LIBA32ANDROID_STATUS_OK) {
        liba32android_runtime_destroy(runtime);
        return fail("public runtime code unmap failed", error_storage);
    }

    liba32android_runtime_destroy(runtime);
    puts("public.embedding.api_version=1");
    puts("public.embedding.arm_result=42");
    puts("public.embedding.svc=0x12");
    puts("public.embedding.structured_error=PASS");
    puts("public.embedding.status=PASS");
    return 0;
}
