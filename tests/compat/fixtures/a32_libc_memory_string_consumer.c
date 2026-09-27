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
__attribute__((visibility("default")))
void* memmem(const void* haystack, fixture_size_t haystack_length,
             const void* needle, fixture_size_t needle_length);
__attribute__((visibility("default")))
char* strcpy(char* destination, const char* source);
__attribute__((visibility("default")))
char* strncpy(char* destination, const char* source, fixture_size_t count);
__attribute__((visibility("default")))
int atoi(const char* text);
__attribute__((visibility("default")))
long strtol(const char* text, char** endptr, int base);
__attribute__((visibility("default")))
int* __errno(void);

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

__attribute__((visibility("default"), noinline))
void* fixture_memmem(const void* haystack, fixture_size_t haystack_length,
                     const void* needle, fixture_size_t needle_length) {
    return memmem(haystack, haystack_length, needle, needle_length);
}

__attribute__((visibility("default"), noinline))
char* fixture_strcpy(char* destination, const char* source) {
    return strcpy(destination, source);
}

__attribute__((visibility("default"), noinline))
char* fixture_strncpy(char* destination, const char* source, fixture_size_t count) {
    return strncpy(destination, source, count);
}

__attribute__((visibility("default"), noinline))
int fixture_atoi(const char* text) {
    return atoi(text);
}

__attribute__((visibility("default"), noinline))
long fixture_strtol(const char* text, char** endptr, int base) {
    return strtol(text, endptr, base);
}

__attribute__((visibility("default"), noinline))
int fixture_errno_read(void) {
    return *__errno();
}
