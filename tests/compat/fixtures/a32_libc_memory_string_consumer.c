// Freestanding ARMv7/Android consumer for the feature-032 partial libc shim.
// -fno-builtin is required so these wrappers retain ordinary dynamic imports.

typedef __SIZE_TYPE__ fixture_size_t;

__attribute__((visibility("default")))
void* memcpy(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void* memmove(void* destination, const void* source, fixture_size_t count);
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
__attribute__((visibility("default")))
void* malloc(fixture_size_t size);
__attribute__((visibility("default")))
void* calloc(fixture_size_t count, fixture_size_t size);
__attribute__((visibility("default")))
void* realloc(void* pointer, fixture_size_t size);
__attribute__((visibility("default")))
void free(void* pointer);
__attribute__((visibility("default")))
int __aeabi_atexit(
    void* object,
    void (*destructor)(void*),
    void* dso_handle);
__attribute__((visibility("default")))
void __cxa_finalize(void* dso_handle);
__attribute__((visibility("default")))
int pthread_mutex_init(void* mutex, const void* attr);
__attribute__((visibility("default")))
int pthread_mutex_destroy(void* mutex);
__attribute__((visibility("default")))
int pthread_mutex_lock(void* mutex);
__attribute__((visibility("default")))
int pthread_mutex_trylock(void* mutex);
__attribute__((visibility("default")))
int pthread_mutex_unlock(void* mutex);
__attribute__((visibility("default")))
int sem_init(void* semaphore, int pshared, unsigned int value);
__attribute__((visibility("default")))
int sem_destroy(void* semaphore);
__attribute__((visibility("default")))
int sem_wait(void* semaphore);
__attribute__((visibility("default")))
int sem_post(void* semaphore);

__attribute__((visibility("default")))
void __aeabi_memcpy(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memcpy4(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memcpy8(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memmove(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memmove4(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memmove8(void* destination, const void* source, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memset(void* destination, fixture_size_t count, int value);
__attribute__((visibility("default")))
void __aeabi_memset4(void* destination, fixture_size_t count, int value);
__attribute__((visibility("default")))
void __aeabi_memset8(void* destination, fixture_size_t count, int value);
__attribute__((visibility("default")))
void __aeabi_memclr(void* destination, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memclr4(void* destination, fixture_size_t count);
__attribute__((visibility("default")))
void __aeabi_memclr8(void* destination, fixture_size_t count);

__attribute__((visibility("default"), noinline))
void* fixture_memcpy(void* destination, const void* source, fixture_size_t count) {
    return memcpy(destination, source, count);
}

__attribute__((visibility("default"), noinline))
void* fixture_memmove(void* destination, const void* source, fixture_size_t count) {
    return memmove(destination, source, count);
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

__attribute__((visibility("default"), noinline))
void* fixture_malloc(fixture_size_t size) {
    return malloc(size);
}

__attribute__((visibility("default"), noinline))
void* fixture_calloc(fixture_size_t count, fixture_size_t size) {
    return calloc(count, size);
}

__attribute__((visibility("default"), noinline))
void* fixture_realloc(void* pointer, fixture_size_t size) {
    return realloc(pointer, size);
}

__attribute__((visibility("default"), noinline))
void fixture_free(void* pointer) {
    free(pointer);
}

__attribute__((visibility("default"), noinline))
int fixture_aeabi_atexit(
    void* object,
    void (*destructor)(void*),
    void* dso_handle) {
    return __aeabi_atexit(object, destructor, dso_handle);
}

__attribute__((visibility("default")))
unsigned int fixture_dso_handle;

__attribute__((visibility("default"), noinline))
void fixture_registered_destructor(void* object) {
    *(volatile unsigned int*)object = 0xC0DEC0DEU;
}

__attribute__((visibility("default"), noinline))
void fixture_cxa_finalize(void* dso_handle) {
    __cxa_finalize(dso_handle);
}

__attribute__((visibility("default"), noinline))
void fixture_on_dlclose(void) {
    __cxa_finalize(&fixture_dso_handle);
}

__attribute__((section(".fini_array"), used))
static void (*const fixture_fini_entry)(void) = fixture_on_dlclose;

__attribute__((visibility("default"), noinline))
void fixture_aeabi_memcpy(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memcpy(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memcpy4(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memcpy4(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memcpy8(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memcpy8(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memmove(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memmove(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memmove4(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memmove4(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memmove8(void* destination, const void* source, fixture_size_t count) {
    __aeabi_memmove8(destination, source, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memset(void* destination, fixture_size_t count, int value) {
    __aeabi_memset(destination, count, value);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memset4(void* destination, fixture_size_t count, int value) {
    __aeabi_memset4(destination, count, value);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memset8(void* destination, fixture_size_t count, int value) {
    __aeabi_memset8(destination, count, value);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memclr(void* destination, fixture_size_t count) {
    __aeabi_memclr(destination, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memclr4(void* destination, fixture_size_t count) {
    __aeabi_memclr4(destination, count);
}
__attribute__((visibility("default"), noinline))
void fixture_aeabi_memclr8(void* destination, fixture_size_t count) {
    __aeabi_memclr8(destination, count);
}

__attribute__((visibility("default"), noinline))
int fixture_pthread_mutex_init(void* mutex, const void* attr) {
    return pthread_mutex_init(mutex, attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutex_destroy(void* mutex) {
    return pthread_mutex_destroy(mutex);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutex_lock(void* mutex) {
    return pthread_mutex_lock(mutex);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutex_trylock(void* mutex) {
    return pthread_mutex_trylock(mutex);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutex_unlock(void* mutex) {
    return pthread_mutex_unlock(mutex);
}
__attribute__((visibility("default"), noinline))
int fixture_sem_init(void* semaphore, int pshared, unsigned int value) {
    return sem_init(semaphore, pshared, value);
}
__attribute__((visibility("default"), noinline))
int fixture_sem_destroy(void* semaphore) {
    return sem_destroy(semaphore);
}
__attribute__((visibility("default"), noinline))
int fixture_sem_wait(void* semaphore) {
    return sem_wait(semaphore);
}
__attribute__((visibility("default"), noinline))
int fixture_sem_post(void* semaphore) {
    return sem_post(semaphore);
}
