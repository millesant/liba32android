// Freestanding ARMv7/Android consumer for the bounded liblog shim.
// Linking against the generated liblog.so must create one DT_NEEDED edge and
// eager R_ARM_JUMP_SLOT references for write/print/vprint.

#include <stdarg.h>

__attribute__((visibility("default")))
int __android_log_write(int priority, const char* tag, const char* text);

__attribute__((visibility("default")))
int __android_log_print(
    int priority,
    const char* tag,
    const char* format,
    ...);

__attribute__((visibility("default")))
int __android_log_vprint(
    int priority,
    const char* tag,
    const char* format,
    va_list args);

__attribute__((visibility("default"), noinline))
int fixture_android_log_write(const char* tag, const char* text) {
    return __android_log_write(4, tag, text);
}

__attribute__((visibility("default"), noinline))
int fixture_android_log_print(const char* tag, const char* text) {
    return __android_log_print(
        5,
        tag,
        "guest=%s value=%d hex=%#x",
        text,
        -7,
        42U);
}

__attribute__((noinline))
static int fixture_android_log_vprint_forward(
    const char* tag,
    const char* format,
    ...) {
    va_list args;
    va_start(args, format);
    const int result = __android_log_vprint(6, tag, format, args);
    va_end(args);
    return result;
}

__attribute__((visibility("default"), noinline))
int fixture_android_log_vprint(const char* tag, const char* text) {
    return fixture_android_log_vprint_forward(
        tag,
        "width=%8.3s signed=%lld ptr=%p",
        text,
        -2LL,
        (void*)0x1234U);
}
