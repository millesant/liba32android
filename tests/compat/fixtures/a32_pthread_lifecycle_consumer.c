// Freestanding ARMv7/Android pthread lifecycle consumer.

typedef __SIZE_TYPE__ fixture_size_t;
typedef unsigned int fixture_pthread_t;

__attribute__((visibility("default")))
int pthread_attr_init(void* attr);
__attribute__((visibility("default")))
int pthread_attr_destroy(void* attr);
__attribute__((visibility("default")))
int pthread_attr_getdetachstate(const void* attr, int* state);
__attribute__((visibility("default")))
int pthread_attr_setdetachstate(void* attr, int state);
__attribute__((visibility("default")))
int pthread_attr_getstacksize(const void* attr, fixture_size_t* stack_size);
__attribute__((visibility("default")))
int pthread_attr_setstacksize(void* attr, fixture_size_t stack_size);
__attribute__((visibility("default")))
int pthread_create(
    fixture_pthread_t* thread,
    const void* attr,
    void* (*start_routine)(void*),
    void* arg);
__attribute__((visibility("default")))
fixture_pthread_t pthread_self(void);
__attribute__((visibility("default")))
int pthread_equal(fixture_pthread_t one, fixture_pthread_t two);
__attribute__((visibility("default")))
void pthread_exit(void* value);

__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_init(void* attr) {
    return pthread_attr_init(attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_destroy(void* attr) {
    return pthread_attr_destroy(attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_getdetachstate(const void* attr, int* state) {
    return pthread_attr_getdetachstate(attr, state);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_setdetachstate(void* attr, int state) {
    return pthread_attr_setdetachstate(attr, state);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_getstacksize(const void* attr, fixture_size_t* stack_size) {
    return pthread_attr_getstacksize(attr, stack_size);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_attr_setstacksize(void* attr, fixture_size_t stack_size) {
    return pthread_attr_setstacksize(attr, stack_size);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_create(
    fixture_pthread_t* thread,
    const void* attr,
    void* (*start_routine)(void*),
    void* arg) {
    return pthread_create(thread, attr, start_routine, arg);
}
__attribute__((visibility("default"), noinline))
fixture_pthread_t fixture_pthread_self(void) {
    return pthread_self();
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_equal(fixture_pthread_t one, fixture_pthread_t two) {
    return pthread_equal(one, two);
}
__attribute__((visibility("default"), noinline))
void fixture_pthread_exit(void* value) {
    pthread_exit(value);
}

__attribute__((visibility("default"), noinline))
void* fixture_pthread_start_return(void* arg) {
    return arg;
}

__attribute__((visibility("default"), noinline))
void* fixture_pthread_start_exit(void* arg) {
    pthread_exit(arg);
    return (void*)0;
}
