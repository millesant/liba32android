#include <signal.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <sys/system_properties.h>

#include "cpu/a32_cpu.h"
#include "memory/guest_memory.h"

#if !defined(__ANDROID__) || !defined(__aarch64__)
#error "android_runtime_smoke must be built for Android arm64-v8a"
#endif

namespace {

constexpr std::size_t kPathCapacity = 768;
FILE* g_log_file = nullptr;
volatile sig_atomic_t g_crash_log_fd = -1;

void tee_vprintf(FILE* console, const char* format, va_list args) {
    va_list file_args;
    va_copy(file_args, args);
    std::vfprintf(console, format, args);
    std::fflush(console);
    if (g_log_file != nullptr) {
        std::vfprintf(g_log_file, format, file_args);
        std::fflush(g_log_file);
    }
    va_end(file_args);
}

void log_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    tee_vprintf(stdout, format, args);
    va_end(args);
}

void error_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    tee_vprintf(stderr, format, args);
    va_end(args);
}

void safe_write_all(int fd, const char* data, std::size_t size) {
    while (size != 0) {
        const ssize_t written = write(fd, data, size);
        if (written > 0) {
            data += written;
            size -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

char* append_literal(char* cursor, char* end, const char* text) {
    while (*text != '\0' && cursor < end) {
        *cursor++ = *text++;
    }
    return cursor;
}

char* append_unsigned(char* cursor, char* end, unsigned long long value) {
    char reversed[32]{};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));
    while (count != 0 && cursor < end) {
        *cursor++ = reversed[--count];
    }
    return cursor;
}

char* append_hex(char* cursor, char* end, std::uintptr_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    char reversed[2 * sizeof(std::uintptr_t)]{};
    std::size_t count = 0;
    do {
        reversed[count++] = digits[value & 0xfU];
        value >>= 4U;
    } while (value != 0 && count < sizeof(reversed));
    cursor = append_literal(cursor, end, "0x");
    while (count != 0 && cursor < end) {
        *cursor++ = reversed[--count];
    }
    return cursor;
}

void crash_handler(int signal_number, siginfo_t* info, void*) {
    char message[256]{};
    char* cursor = message;
    char* const end = message + sizeof(message) - 1;
    cursor = append_literal(cursor, end, "A32CRASH|component=android_runtime_smoke|signal=");
    cursor = append_unsigned(cursor, end, static_cast<unsigned long long>(signal_number));
    cursor = append_literal(cursor, end, "|pid=");
    cursor = append_unsigned(cursor, end, static_cast<unsigned long long>(getpid()));
    if (info != nullptr) {
        cursor = append_literal(cursor, end, "|addr=");
        cursor = append_hex(cursor, end, reinterpret_cast<std::uintptr_t>(info->si_addr));
    }
    cursor = append_literal(cursor, end, "\n");
    const std::size_t size = static_cast<std::size_t>(cursor - message);
    safe_write_all(STDERR_FILENO, message, size);
    const int fd = static_cast<int>(g_crash_log_fd);
    if (fd >= 0 && fd != STDERR_FILENO) {
        safe_write_all(fd, message, size);
    }

    // Dynarmic's POSIX fastmem SIGSEGV handler saves the previously installed
    // action and chains to it by calling the function pointer directly for an
    // unhandled fault. In that chained path the kernel cannot apply
    // SA_RESETHAND for us, so explicitly restore the default disposition.
    // Recoverable fastmem faults never reach this callback: Dynarmic handles
    // them first and rewrites the JIT context to its fallback path.
    signal(signal_number, SIG_DFL);
}

[[noreturn]] void run_crash_test() {
    log_printf("crash_test.status=ARMED\n");
    log_printf("crash_test.signal=SIGABRT\n");
    std::abort();
}

bool install_crash_handlers() {
    constexpr std::array<int, 5> signals{SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
    struct sigaction action {};
    action.sa_sigaction = crash_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (const int signal_number : signals) {
        if (sigaction(signal_number, &action, nullptr) != 0) {
            return false;
        }
    }
    return true;
}

bool ensure_parent_directories(const char* path) {
    char buffer[kPathCapacity]{};
    if (path == nullptr || path[0] == '\0' || std::strlen(path) >= sizeof(buffer)) {
        errno = EINVAL;
        return false;
    }
    std::strcpy(buffer, path);
    char* const last_slash = std::strrchr(buffer, '/');
    if (last_slash == nullptr || last_slash == buffer) {
        return true;
    }
    *last_slash = '\0';
    for (char* cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (mkdir(buffer, 0775) != 0 && errno != EEXIST) {
            *cursor = '/';
            return false;
        }
        *cursor = '/';
    }
    return mkdir(buffer, 0775) == 0 || errno == EEXIST;
}

bool open_log(const char* path) {
    if (!ensure_parent_directories(path)) {
        return false;
    }
    FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::setvbuf(file, nullptr, _IONBF, 0);
    g_log_file = file;
    g_crash_log_fd = fileno(file);
    return true;
}

bool try_log_path(char* selected, std::size_t selected_size, const char* path) {
    if (path == nullptr || path[0] == '\0' || std::strlen(path) >= selected_size) {
        return false;
    }
    if (!open_log(path)) {
        return false;
    }
    std::strcpy(selected, path);
    return true;
}

void configure_logging(const char* requested_path, char* selected, std::size_t selected_size) {
    const char* env_path = std::getenv("LIBA32ANDROID_LOG_FILE");
    const char* explicit_path = requested_path != nullptr ? requested_path : env_path;
    if (try_log_path(selected, selected_size, explicit_path)) {
        return;
    }

    const char* home = std::getenv("HOME");
    char candidate[kPathCapacity]{};
    if (home != nullptr && home[0] != '\0') {
        const int written = std::snprintf(candidate, sizeof(candidate),
                                          "%s/storage/downloads/liba32android-runtime-smoke.log", home);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(candidate) &&
            try_log_path(selected, selected_size, candidate)) {
            return;
        }
    }

    if (try_log_path(selected, selected_size,
                     "/sdcard/Download/liba32android/runtime-smoke.log")) {
        return;
    }

    if (home != nullptr && home[0] != '\0') {
        const int written = std::snprintf(candidate, sizeof(candidate),
                                          "%s/liba32android/runtime-smoke.log", home);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(candidate) &&
            try_log_path(selected, selected_size, candidate)) {
            return;
        }
    }

    try_log_path(selected, selected_size, "./liba32android-runtime-smoke.log");
}

void print_property(const char* property, const char* key) {
    char value[PROP_VALUE_MAX]{};
    const int length = __system_property_get(property, value);
    if (length > 0) {
        log_printf("%s=%s\n", key, value);
    } else {
        log_printf("%s=unavailable\n", key);
    }
}

void print_environment() {
#ifdef __ANDROID_API__
    log_printf("android.ndk_api=%d\n", __ANDROID_API__);
#else
    log_printf("android.ndk_api=unknown\n");
#endif
    print_property("ro.build.version.sdk", "android.runtime_sdk");
    print_property("ro.build.version.release", "android.release");
    log_printf("arch=aarch64\n");
    log_printf("page_size=%ld\n", sysconf(_SC_PAGESIZE));

    utsname info{};
    if (uname(&info) == 0) {
        log_printf("kernel.release=%s\n", info.release);
        log_printf("kernel.version=%s\n", info.version);
    }
}

bool execution_ok(const liba32android::cpu::ExecutionResult& result, std::size_t expected) {
    return !result.exception_raised && !result.memory_fault && result.instructions_executed == expected;
}

int run_smoke(bool exercise_fault) {
    using liba32android::memory::MappedGuestMemory;
    using liba32android::memory::MemoryPermission;

    MappedGuestMemory memory;
    const std::size_t page = memory.page_size();
    const auto fastmem = memory.fastmem_base();
    log_printf("fastmem.available=%s\n", fastmem.has_value() ? "true" : "false");
    if (fastmem.has_value()) {
        log_printf("fastmem.base=0x%llx\n", static_cast<unsigned long long>(*fastmem));
    }

    const auto rw = MemoryPermission::Read | MemoryPermission::Write;
    const auto rx = MemoryPermission::Read | MemoryPermission::Execute;
    const std::uint32_t return_code = static_cast<std::uint32_t>(page * 16);
    const std::uint32_t memory_code = static_cast<std::uint32_t>(page * 17);
    const std::uint32_t data = static_cast<std::uint32_t>(page * 32);

    constexpr std::array<std::uint8_t, 4> return_42{0x2A, 0x00, 0xA0, 0xE3};  // mov r0,#42
    if (!memory.map(return_code, page, rw) || !memory.write(return_code, return_42) ||
        !memory.protect(return_code, page, rx)) {
        error_printf("A32ERR|component=android_runtime_smoke|code=MAP_RETURN42|message=failed to prepare code page\n");
        return 1;
    }

    liba32android::cpu::ExecutionRequest return_request{};
    return_request.entry_pc = return_code;
    const auto return_result = liba32android::cpu::execute(memory, return_request);
    log_printf("a32.return42.fastmem_enabled=%s\n", return_result.fastmem_enabled ? "true" : "false");
    log_printf("a32.return42.r0=%u\n", return_result.regs[0]);
    log_printf("a32.return42.code_callbacks=%zu\n", return_result.code_read_callbacks);
    log_printf("a32.return42.status=%s\n",
               execution_ok(return_result, 1) && return_result.regs[0] == 42 ? "PASS" : "FAIL");
    if (!execution_ok(return_result, 1) || return_result.regs[0] != 42 || !return_result.fastmem_enabled) {
        return 1;
    }

    constexpr std::array<std::uint8_t, 8> load_store{
        0x00, 0x00, 0x81, 0xE5,  // str r0,[r1]
        0x00, 0x20, 0x91, 0xE5,  // ldr r2,[r1]
    };
    if (!memory.map(memory_code, page, rw) || !memory.write(memory_code, load_store) ||
        !memory.protect(memory_code, page, rx) || !memory.map(data, page, rw)) {
        error_printf("A32ERR|component=android_runtime_smoke|code=MAP_LOAD_STORE|message=failed to prepare mapped pages\n");
        return 1;
    }

    liba32android::cpu::ExecutionRequest memory_request{};
    memory_request.entry_pc = memory_code;
    memory_request.regs[0] = 0x12345678;
    memory_request.regs[1] = data;
    memory_request.instruction_count = 2;
    const auto memory_result = liba32android::cpu::execute(memory, memory_request);

    std::array<std::uint8_t, 4> stored_bytes{};
    const bool stored_ok = memory.read(data, stored_bytes);
    const std::uint32_t stored = static_cast<std::uint32_t>(stored_bytes[0]) |
                                 static_cast<std::uint32_t>(stored_bytes[1]) << 8 |
                                 static_cast<std::uint32_t>(stored_bytes[2]) << 16 |
                                 static_cast<std::uint32_t>(stored_bytes[3]) << 24;
    const bool fastmem_direct = memory_result.data_read_callbacks == 0 && memory_result.data_write_callbacks == 0;
    const bool memory_ok = execution_ok(memory_result, 2) && memory_result.fastmem_enabled &&
                           memory_result.regs[2] == 0x12345678 && stored_ok &&
                           stored == 0x12345678 && fastmem_direct;

    log_printf("a32.memory.fastmem_enabled=%s\n", memory_result.fastmem_enabled ? "true" : "false");
    log_printf("a32.memory.data_read_callbacks=%zu\n", memory_result.data_read_callbacks);
    log_printf("a32.memory.data_write_callbacks=%zu\n", memory_result.data_write_callbacks);
    log_printf("a32.memory.r2=0x%08x\n", memory_result.regs[2]);
    log_printf("a32.memory.stored=0x%08x\n", stored);
    log_printf("a32.memory.fastmem_direct=%s\n", fastmem_direct ? "true" : "false");
    log_printf("a32.memory.status=%s\n", memory_ok ? "PASS" : "FAIL");
    if (!memory_ok) {
        return 1;
    }

    if (exercise_fault) {
        constexpr std::array<std::uint8_t, 4> faulting_load{0x00, 0x00, 0x91, 0xE5};  // ldr r0,[r1]
        const std::uint32_t fault_code = static_cast<std::uint32_t>(page * 18);
        const std::uint32_t unmapped = static_cast<std::uint32_t>(page * 48);
        if (!memory.map(fault_code, page, rw) || !memory.write(fault_code, faulting_load) ||
            !memory.protect(fault_code, page, rx)) {
            return 1;
        }
        liba32android::cpu::ExecutionRequest fault_request{};
        fault_request.entry_pc = fault_code;
        fault_request.regs[1] = unmapped;
        const auto fault_result = liba32android::cpu::execute(memory, fault_request);
        const bool fallback_ok = fault_result.fastmem_enabled && fault_result.memory_fault &&
                                 fault_result.data_read_callbacks != 0;
        log_printf("a32.fastmem_fault.memory_fault=%s\n", fault_result.memory_fault ? "true" : "false");
        log_printf("a32.fastmem_fault.data_read_callbacks=%zu\n", fault_result.data_read_callbacks);
        log_printf("a32.fastmem_fault.status=%s\n", fallback_ok ? "PASS" : "FAIL");
        if (!fallback_ok) {
            return 1;
        }
    } else {
        log_printf("a32.fastmem_fault.status=NOT_RUN\n");
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* log_path = nullptr;
    bool exercise_fault = false;
    bool crash_test = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--log-file") == 0 && index + 1 < argc) {
            log_path = argv[++index];
        } else if (std::strcmp(argv[index], "--exercise-fastmem-fault") == 0) {
            exercise_fault = true;
        } else if (std::strcmp(argv[index], "--crash-test") == 0) {
            crash_test = true;
        } else {
            std::fprintf(stderr,
                         "usage: android_runtime_smoke [--log-file PATH] "
                         "[--exercise-fastmem-fault | --crash-test]\n");
            return 2;
        }
    }

    if (exercise_fault && crash_test) {
        std::fprintf(stderr,
                     "--exercise-fastmem-fault and --crash-test are mutually exclusive\n");
        return 2;
    }

    char selected_log[kPathCapacity]{};
    configure_logging(log_path, selected_log, sizeof(selected_log));
    if (g_log_file != nullptr) {
        log_printf("diagnostics.file_log=enabled\n");
        log_printf("diagnostics.log_file=%s\n", selected_log);
    } else {
        log_printf("diagnostics.file_log=unavailable\n");
    }
    const bool crash_handlers_installed = install_crash_handlers();
    log_printf("diagnostics.crash_markers=%s\n", crash_handlers_installed ? "enabled" : "partial");
    log_printf("runtime_smoke.version=1\n");
    print_environment();
    if (crash_test && !crash_handlers_installed) {
        error_printf("A32ERR|component=android_runtime_smoke|code=CRASH_HANDLER_SETUP|"
                     "message=crash test requires all signal handlers\n");
        return 1;
    }
    if (crash_test) {
        run_crash_test();
    }

    try {
        const int result = run_smoke(exercise_fault);
        log_printf("runtime_smoke.complete=%s\n", result == 0 ? "true" : "false");
        return result;
    } catch (const std::exception& exception) {
        error_printf("A32ERR|component=android_runtime_smoke|code=EXCEPTION|message=%s\n", exception.what());
        log_printf("runtime_smoke.complete=false\n");
        return 1;
    }
}
