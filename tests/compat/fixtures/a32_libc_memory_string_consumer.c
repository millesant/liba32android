// Freestanding ARMv7/Android consumer for the feature-032 partial libc shim.
// -fno-builtin is required so these wrappers retain ordinary dynamic imports.

typedef __SIZE_TYPE__ fixture_size_t;

__attribute__((visibility("default")))
void* memcpy(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void* memset(void* destination, int value, fixture_size_t count);
__attribute__((visibility("default")))
int memcmp(const void* lhs, const void* rhs, fixture_size_t count);
__attribute__((visibility("default")))
void* memchr(const void* data, int value, fixture_size_t count);
__attribute__((visibility("default")))
fixture_size_t strlen(const char* text);
__attribute__((visibility("default")))
int strcmp(const char* lhs, const char* rhs);
__attribute__((visibility("default")))
int strncmp(const char* lhs, const char* rhs, fixture_size_t count);

__attribute__((visibility("default"), noinline))
void* fixture_memcpy(void* destination, const void* source, fixture_size_t count) {
    return memcpy(destination, source, count);
}

__attribute__((visibility("default"), noinline))
void* fixture_memset(void* destination, int value, fixture_size_t count) {
    return memset(destination, value, count);
}

__attribute__((visibility("default"), noinline))
int fixture_memcmp(const void* lhs, const void* rhs, fixture_size_t count) {
    return memcmp(lhs, rhs, count);
}

__attribute__((visibility("default"), noinline))
void* fixture_memchr(const void* data, int value, fixture_size_t count) {
    return memchr(data, value, count);
}

__attribute__((visibility("default"), noinline))
fixture_size_t fixture_strlen(const char* text) {
    return strlen(text);
}

__attribute__((visibility("default"), noinline))
int fixture_strcmp(const char* lhs, const char* rhs) {
    return strcmp(lhs, rhs);
}

__attribute__((visibility("default"), noinline))
int fixture_strncmp(const char* lhs, const char* rhs, fixture_size_t count) {
    return strncmp(lhs, rhs, count);
}
