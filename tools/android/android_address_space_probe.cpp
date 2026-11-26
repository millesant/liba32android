#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(__ANDROID__) || (!defined(__aarch64__) && !defined(__x86_64__))
#error "android_address_space_probe must be built for Android arm64-v8a or x86_64"
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace {

constexpr std::uint64_t kFourGiB = std::uint64_t{1} << 32;
constexpr std::size_t kMaxPrintedLowMappings = 64;
constexpr std::size_t kPathCapacity = 768;
constexpr const char* kSharedDownloadLog = "/sdcard/Download/liba32android/address-space-probe.log";

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

char* append_unsigned_decimal(char* cursor, char* end, unsigned long long value) {
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
        reversed[count++] = digits[value & 0x0fU];
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

    cursor = append_literal(cursor, end, "A32CRASH|component=android_address_space_probe|signal=");
    cursor = append_unsigned_decimal(cursor, end, static_cast<unsigned long long>(signal_number));
    cursor = append_literal(cursor, end, "|pid=");
    cursor = append_unsigned_decimal(cursor, end, static_cast<unsigned long long>(getpid()));
    if (info != nullptr) {
        cursor = append_literal(cursor, end, "|addr=");
        cursor = append_hex(cursor, end, reinterpret_cast<std::uintptr_t>(info->si_addr));
    }
    cursor = append_literal(cursor, end, "\n");

    const std::size_t size = static_cast<std::size_t>(cursor - message);
    safe_write_all(STDERR_FILENO, message, size);

    const int crash_fd = static_cast<int>(g_crash_log_fd);
    if (crash_fd >= 0 && crash_fd != STDERR_FILENO) {
        safe_write_all(crash_fd, message, size);
    }

    // SA_RESETHAND restores the default disposition before this handler runs.
    // Returning lets a synchronous fault recur (or abort continue), preserving
    // Android's normal fatal-signal/tombstone path after our minimal marker.
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

    bool installed = true;
    for (const int signal_number : signals) {
        if (sigaction(signal_number, &action, nullptr) != 0) {
            installed = false;
            error_printf("diagnostics.crash_handler.%d=failed\n", signal_number);
            error_printf("diagnostics.crash_handler.%d.errno=%d\n", signal_number, errno);
        }
    }
    return installed;
}

bool ensure_parent_directories(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        errno = EINVAL;
        return false;
    }

    char buffer[kPathCapacity]{};
    if (std::snprintf(buffer, sizeof(buffer), "%s", path) < 0 || std::strlen(path) >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return false;
    }

    char* const last_slash = std::strrchr(buffer, '/');
    if (last_slash == nullptr) {
        return true;
    }
    if (last_slash == buffer) {
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

bool open_log_file(const char* path) {
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

struct LoggingState {
    bool file_enabled{};
    bool shared_download_selected{};
    int shared_download_error{};
    int fallback_error{};
    char path[kPathCapacity]{};
};

bool copy_path(char* destination, std::size_t destination_size, const char* source) {
    if (source == nullptr || std::strlen(source) >= destination_size) {
        errno = ENAMETOOLONG;
        return false;
    }
    std::strcpy(destination, source);
    return true;
}

LoggingState configure_logging(const char* requested_path, bool disable_file_log) {
    LoggingState state{};
    if (disable_file_log) {
        return state;
    }

    const char* environment_path = std::getenv("LIBA32ANDROID_LOG_FILE");
    const char* explicit_path = requested_path != nullptr ? requested_path : environment_path;
    if (explicit_path != nullptr && explicit_path[0] != '\0') {
        if (!copy_path(state.path, sizeof(state.path), explicit_path)) {
            state.fallback_error = errno;
            return state;
        }
        if (open_log_file(state.path)) {
            state.file_enabled = true;
        } else {
            state.fallback_error = errno;
        }
        return state;
    }

    copy_path(state.path, sizeof(state.path), kSharedDownloadLog);
    if (open_log_file(state.path)) {
        state.file_enabled = true;
        state.shared_download_selected = true;
        return state;
    }
    state.shared_download_error = errno;

    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        const int written = std::snprintf(state.path, sizeof(state.path), "%s/liba32android/address-space-probe.log", home);
        if (written > 0 && static_cast<std::size_t>(written) < sizeof(state.path) && open_log_file(state.path)) {
            state.file_enabled = true;
            return state;
        }
        state.fallback_error = errno;
    }

    copy_path(state.path, sizeof(state.path), "./liba32android-address-space-probe.log");
    if (open_log_file(state.path)) {
        state.file_enabled = true;
        return state;
    }
    state.fallback_error = errno;
    state.path[0] = '\0';
    return state;
}

void print_logging_state(const LoggingState& state, bool file_log_disabled) {
    if (file_log_disabled) {
        log_printf("diagnostics.file_log=disabled\n");
        return;
    }

    if (state.file_enabled) {
        log_printf("diagnostics.file_log=enabled\n");
        log_printf("diagnostics.log_file=%s\n", state.path);
        log_printf("diagnostics.shared_download=%s\n", state.shared_download_selected ? "selected" : "unavailable");
        if (!state.shared_download_selected && state.shared_download_error != 0) {
            log_printf("diagnostics.shared_download.errno=%d\n", state.shared_download_error);
            log_printf("diagnostics.shared_download.error=%s\n", std::strerror(state.shared_download_error));
        }
        return;
    }

    log_printf("diagnostics.file_log=unavailable\n");
    if (state.shared_download_error != 0) {
        log_printf("diagnostics.shared_download=unavailable\n");
        log_printf("diagnostics.shared_download.errno=%d\n", state.shared_download_error);
        log_printf("diagnostics.shared_download.error=%s\n", std::strerror(state.shared_download_error));
    }
    if (state.fallback_error != 0) {
        log_printf("diagnostics.file_log.errno=%d\n", state.fallback_error);
        log_printf("diagnostics.file_log.error=%s\n", std::strerror(state.fallback_error));
    }
}

int anonymous_flags() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    return flags;
}

void print_errno(const char* key, int error) {
    log_printf("%s.errno=%d\n", key, error);
    log_printf("%s.error=%s\n", key, std::strerror(error));
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

void print_environment(long page_size) {
#ifdef __ANDROID_API__
    log_printf("android.ndk_api=%d\n", __ANDROID_API__);
#else
    log_printf("android.ndk_api=unknown\n");
#endif
    print_property("ro.build.version.sdk", "android.runtime_sdk");
    print_property("ro.build.version.release", "android.release");
#if defined(__aarch64__)
    log_printf("arch=aarch64\n");
#elif defined(__x86_64__)
    log_printf("arch=x86_64\n");
#endif
    log_printf("page_size=%ld\n", page_size);

    utsname info{};
    if (uname(&info) == 0) {
        log_printf("kernel.sysname=%s\n", info.sysname);
        log_printf("kernel.release=%s\n", info.release);
        log_printf("kernel.version=%s\n", info.version);
        log_printf("kernel.machine=%s\n", info.machine);
    } else {
        const int error = errno;
        log_printf("kernel.status=unavailable\n");
        print_errno("kernel", error);
    }
}

void print_mmap_min_addr() {
    errno = 0;
    FILE* file = std::fopen("/proc/sys/vm/mmap_min_addr", "r");
    if (file == nullptr) {
        const int error = errno;
        log_printf("mmap_min_addr.status=unavailable\n");
        print_errno("mmap_min_addr", error);
        return;
    }

    unsigned long long value = 0;
    if (std::fscanf(file, "%llu", &value) == 1) {
        log_printf("mmap_min_addr.status=ok\n");
        log_printf("mmap_min_addr.value=%llu\n", value);
    } else {
        log_printf("mmap_min_addr.status=parse_error\n");
    }
    std::fclose(file);
}

void print_low_mappings() {
    errno = 0;
    FILE* file = std::fopen("/proc/self/maps", "r");
    if (file == nullptr) {
        const int error = errno;
        log_printf("low_maps.status=unavailable\n");
        print_errno("low_maps", error);
        return;
    }

    char line[768]{};
    std::size_t count = 0;
    std::size_t printed = 0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5]{};
        if (std::sscanf(line, "%llx-%llx %4s", &start, &end, permissions) != 3) {
            continue;
        }
        if (start >= kFourGiB) {
            continue;
        }

        ++count;
        if (printed < kMaxPrintedLowMappings) {
            const auto clipped_end = end > kFourGiB ? kFourGiB : end;
            log_printf("low_map.%zu=0x%llx-0x%llx,%s\n", printed, start, clipped_end, permissions);
            ++printed;
        }
    }
    std::fclose(file);

    log_printf("low_maps.status=ok\n");
    log_printf("low_maps.count=%zu\n", count);
    log_printf("low_maps.printed=%zu\n", printed);
    log_printf("low_maps.truncated=%s\n", count > printed ? "true" : "false");
}

void probe_fastmem_reservation(std::size_t page_size) {
    const std::size_t length = static_cast<std::size_t>(kFourGiB);
    errno = 0;
    void* mapping = mmap(nullptr, length, PROT_NONE, anonymous_flags(), -1, 0);
    if (mapping == MAP_FAILED) {
        const int error = errno;
        log_printf("fastmem_4g.status=failed\n");
        print_errno("fastmem_4g", error);
        return;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(mapping);
    log_printf("fastmem_4g.status=reserved\n");
    log_printf("fastmem_4g.base=0x%" PRIxPTR "\n", base);
    log_printf("fastmem_4g.end=0x%" PRIx64 "\n", static_cast<std::uint64_t>(base) + kFourGiB);
    log_printf("fastmem_4g.base_below_4g=%s\n", static_cast<std::uint64_t>(base) < kFourGiB ? "true" : "false");

    auto* bytes = static_cast<unsigned char*>(mapping);
    void* sample = bytes + 16 * page_size;
    errno = 0;
    if (mprotect(sample, page_size, PROT_READ | PROT_WRITE) == 0) {
        *static_cast<volatile unsigned char*>(sample) = 0x5a;
        const bool value_ok = *static_cast<volatile unsigned char*>(sample) == 0x5a;
        log_printf("fastmem_4g.commit_page=%s\n", value_ok ? "success" : "value_mismatch");
        if (mprotect(sample, page_size, PROT_NONE) != 0) {
            print_errno("fastmem_4g.reset_page", errno);
        }
    } else {
        const int error = errno;
        log_printf("fastmem_4g.commit_page=failed\n");
        print_errno("fastmem_4g.commit_page", error);
    }

    if (munmap(mapping, length) == 0) {
        log_printf("fastmem_4g.unmap=success\n");
    } else {
        const int error = errno;
        log_printf("fastmem_4g.unmap=failed\n");
        print_errno("fastmem_4g.unmap", error);
    }
}

void probe_fixed_noreplace(std::size_t page_size) {
    constexpr std::array<std::uintptr_t, 6> candidates{
        0x00010000u,
        0x00100000u,
        0x01000000u,
        0x10000000u,
        0x40000000u,
        0x80000000u,
    };

    const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        void* requested = reinterpret_cast<void*>(candidates[index]);
        errno = 0;
        void* mapping = mmap(requested, page_size, PROT_NONE, flags, -1, 0);
        if (mapping == MAP_FAILED) {
            const int error = errno;
            log_printf("fixed_noreplace.%zu.request=0x%" PRIxPTR "\n", index, candidates[index]);
            log_printf("fixed_noreplace.%zu.status=failed\n", index);
            log_printf("fixed_noreplace.%zu.errno=%d\n", index, error);
            log_printf("fixed_noreplace.%zu.error=%s\n", index, std::strerror(error));
            continue;
        }

        const auto actual = reinterpret_cast<std::uintptr_t>(mapping);
        log_printf("fixed_noreplace.%zu.request=0x%" PRIxPTR "\n", index, candidates[index]);
        log_printf("fixed_noreplace.%zu.actual=0x%" PRIxPTR "\n", index, actual);
        if (mapping != requested) {
            log_printf("fixed_noreplace.%zu.status=relocated_flag_not_enforced\n", index);
            munmap(mapping, page_size);
            continue;
        }

        log_printf("fixed_noreplace.%zu.status=exact\n", index);
        errno = 0;
        void* collision = mmap(requested, page_size, PROT_NONE, flags, -1, 0);
        if (collision == MAP_FAILED) {
            const int collision_error = errno;
            log_printf("fixed_noreplace.%zu.collision_errno=%d\n", index, collision_error);
            log_printf("fixed_noreplace.%zu.collision=%s\n", index,
                       collision_error == EEXIST ? "EEXIST" : "failed_other");
        } else {
            const auto collision_actual = reinterpret_cast<std::uintptr_t>(collision);
            log_printf("fixed_noreplace.%zu.collision=unexpected_mapping\n", index);
            log_printf("fixed_noreplace.%zu.collision_actual=0x%" PRIxPTR "\n", index, collision_actual);
            munmap(collision, page_size);
        }
        munmap(mapping, page_size);
    }
}

bool probe_jit_wx(std::size_t page_size, bool execute_generated_code) {
    errno = 0;
    void* mapping = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, anonymous_flags(), -1, 0);
    if (mapping == MAP_FAILED) {
        const int error = errno;
        log_printf("jit_wx.map=failed\n");
        print_errno("jit_wx.map", error);
        log_printf("jit_wx.execute=NOT_RUN\n");
        return false;
    }

#if defined(__aarch64__)
    // AArch64: mov w0, #42; ret
    constexpr std::array<std::uint32_t, 2> code{0x52800540u, 0xd65f03c0u};
#elif defined(__x86_64__)
    // x86-64: mov eax, 42; ret
    constexpr std::array<std::uint8_t, 6> code{0xb8u, 0x2au, 0x00u, 0x00u, 0x00u, 0xc3u};
#endif
    std::memcpy(mapping, code.data(), sizeof(code));
    auto* begin = static_cast<char*>(mapping);
    __builtin___clear_cache(begin, begin + sizeof(code));

    errno = 0;
    if (mprotect(mapping, page_size, PROT_READ | PROT_EXEC) != 0) {
        const int error = errno;
        log_printf("jit_wx.mprotect_rw_to_rx=failed\n");
        print_errno("jit_wx.mprotect_rw_to_rx", error);
        log_printf("jit_wx.execute=NOT_RUN\n");
        munmap(mapping, page_size);
        return false;
    }
    log_printf("jit_wx.mprotect_rw_to_rx=success\n");

    bool execution_ok = true;
    if (execute_generated_code) {
        std::fflush(stdout);
        if (g_log_file != nullptr) {
            std::fflush(g_log_file);
        }
        using GeneratedFunction = int (*)();
        const auto function = reinterpret_cast<GeneratedFunction>(mapping);
        const int result = function();
        log_printf("jit_wx.execute=RUN\n");
        log_printf("jit_wx.result=%d\n", result);
        execution_ok = result == 42;
        log_printf("jit_wx.result_check=%s\n", execution_ok ? "PASS" : "FAIL");
    } else {
        log_printf("jit_wx.execute=NOT_RUN\n");
    }

    if (munmap(mapping, page_size) != 0) {
        print_errno("jit_wx.unmap", errno);
    }
    return execution_ok;
}

void print_usage() {
    error_printf(
        "usage: android_address_space_probe [--execute-generated-code | --crash-test] "
        "[--log-file PATH] [--no-log-file]\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool execute_generated_code = false;
    bool disable_file_log = false;
    bool crash_test = false;
    const char* requested_log_file = nullptr;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--execute-generated-code") == 0) {
            execute_generated_code = true;
            continue;
        }
        if (std::strcmp(argv[index], "--no-log-file") == 0) {
            disable_file_log = true;
            continue;
        }
        if (std::strcmp(argv[index], "--crash-test") == 0) {
            crash_test = true;
            continue;
        }
        if (std::strcmp(argv[index], "--log-file") == 0 && index + 1 < argc) {
            requested_log_file = argv[++index];
            continue;
        }
        print_usage();
        return 2;
    }

    if (execute_generated_code && crash_test) {
        print_usage();
        return 2;
    }

    if (disable_file_log && requested_log_file != nullptr) {
        print_usage();
        return 2;
    }

    const bool explicit_log_requested = requested_log_file != nullptr || std::getenv("LIBA32ANDROID_LOG_FILE") != nullptr;
    const LoggingState logging = configure_logging(requested_log_file, disable_file_log);
    if (explicit_log_requested && !disable_file_log && !logging.file_enabled) {
        error_printf("A32ERR|component=diagnostics|code=LOG_FILE_OPEN_FAILED|message=could not open requested log file\n");
        if (logging.fallback_error != 0) {
            error_printf("diagnostics.file_log.errno=%d\n", logging.fallback_error);
            error_printf("diagnostics.file_log.error=%s\n", std::strerror(logging.fallback_error));
        }
        return 2;
    }

    print_logging_state(logging, disable_file_log);
    const bool crash_handlers_installed = install_crash_handlers();
    log_printf("diagnostics.crash_markers=%s\n", crash_handlers_installed ? "enabled" : "partial");
    if (crash_test && !crash_handlers_installed) {
        error_printf("A32ERR|component=diagnostics|code=CRASH_HANDLER_SETUP|"
                     "message=crash test requires all signal handlers\n");
        return 1;
    }
    if (crash_test) {
        run_crash_test();
    }

    static_assert(sizeof(std::size_t) >= 8, "4 GiB probe requires a 64-bit process");

    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        error_printf("A32ERR|component=probe|code=PAGE_SIZE_UNAVAILABLE|message=sysconf(_SC_PAGESIZE) failed\n");
        return 2;
    }
    const auto page_size = static_cast<std::size_t>(page_size_raw);

    log_printf("probe.version=2\n");
    print_environment(page_size_raw);
    print_mmap_min_addr();
    print_low_mappings();
    probe_fastmem_reservation(page_size);
    probe_fixed_noreplace(page_size);
    const bool execution_ok = probe_jit_wx(page_size, execute_generated_code);
    log_printf("probe.complete=true\n");

    g_crash_log_fd = -1;
    if (g_log_file != nullptr) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }

    return execute_generated_code && !execution_ok ? 1 : 0;
}
