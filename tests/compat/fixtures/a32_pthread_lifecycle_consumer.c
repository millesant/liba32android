// Freestanding ARMv7/Android pthread lifecycle consumer.

typedef __SIZE_TYPE__ fixture_size_t;
typedef unsigned int fixture_pthread_t;
typedef struct fixture_sched_param {
    int sched_priority;
} fixture_sched_param;
typedef struct fixture_pthread_cleanup {
    struct fixture_pthread_cleanup* previous;
    void (*routine)(void*);
    void* argument;
} fixture_pthread_cleanup;

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
__attribute__((visibility("default")))
int pthread_join(fixture_pthread_t thread, void** value);
__attribute__((visibility("default")))
int pthread_detach(fixture_pthread_t thread);
__attribute__((visibility("default")))
int pthread_getschedparam(
    fixture_pthread_t thread,
    int* policy,
    fixture_sched_param* param);
__attribute__((visibility("default")))
int pthread_setschedparam(
    fixture_pthread_t thread,
    int policy,
    const fixture_sched_param* param);
__attribute__((visibility("default")))
int pthread_setname_np(fixture_pthread_t thread, const char* name);
__attribute__((visibility("default")))
void __pthread_cleanup_push(
    fixture_pthread_cleanup* cleanup,
    void (*routine)(void*),
    void* argument);
__attribute__((visibility("default")))
void __pthread_cleanup_pop(fixture_pthread_cleanup* cleanup, int execute);
__attribute__((visibility("default")))
int pthread_cond_init(void* cond, const void* attr);
__attribute__((visibility("default")))
int pthread_cond_destroy(void* cond);
__attribute__((visibility("default")))
int pthread_cond_wait(void* cond, void* mutex);
__attribute__((visibility("default")))
int pthread_cond_timedwait(void* cond, void* mutex, const void* abstime);
__attribute__((visibility("default")))
int pthread_cond_signal(void* cond);
__attribute__((visibility("default")))
int pthread_cond_broadcast(void* cond);
__attribute__((visibility("default")))
int pthread_mutexattr_init(void* attr);
__attribute__((visibility("default")))
int pthread_mutexattr_destroy(void* attr);
__attribute__((visibility("default")))
int pthread_mutexattr_settype(void* attr, int type);
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
int pthread_once(void* once_control, void (*init_routine)(void));
__attribute__((visibility("default")))
int pthread_rwlock_init(void* rwlock, const void* attr);
__attribute__((visibility("default")))
int pthread_rwlock_destroy(void* rwlock);
__attribute__((visibility("default")))
int pthread_rwlock_rdlock(void* rwlock);
__attribute__((visibility("default")))
int pthread_rwlock_wrlock(void* rwlock);
__attribute__((visibility("default")))
int pthread_rwlock_unlock(void* rwlock);

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
int fixture_pthread_join(fixture_pthread_t thread, void** value) {
    return pthread_join(thread, value);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_detach(fixture_pthread_t thread) {
    return pthread_detach(thread);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_getschedparam(
    fixture_pthread_t thread,
    int* policy,
    fixture_sched_param* param) {
    return pthread_getschedparam(thread, policy, param);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_setschedparam(
    fixture_pthread_t thread,
    int policy,
    const fixture_sched_param* param) {
    return pthread_setschedparam(thread, policy, param);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_setname_np(fixture_pthread_t thread, const char* name) {
    return pthread_setname_np(thread, name);
}

__attribute__((noinline))
static void fixture_cleanup_marker(void* argument) {
    *(volatile unsigned int*)argument = 0x59c1ea59U;
}

__attribute__((visibility("default"), noinline))
int fixture_pthread_cleanup_push_pop(
    fixture_pthread_cleanup* cleanup,
    volatile unsigned int* marker) {
    *marker = 0U;
    __pthread_cleanup_push(cleanup, fixture_cleanup_marker, (void*)marker);
    __pthread_cleanup_pop(cleanup, 1);
    return *marker == 0x59c1ea59U ? 0 : 1;
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_init(void* cond, const void* attr) {
    return pthread_cond_init(cond, attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_destroy(void* cond) {
    return pthread_cond_destroy(cond);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_wait(void* cond, void* mutex) {
    return pthread_cond_wait(cond, mutex);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_timedwait(void* cond, void* mutex, const void* abstime) {
    return pthread_cond_timedwait(cond, mutex, abstime);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_signal(void* cond) {
    return pthread_cond_signal(cond);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_cond_broadcast(void* cond) {
    return pthread_cond_broadcast(cond);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutexattr_init(void* attr) {
    return pthread_mutexattr_init(attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutexattr_destroy(void* attr) {
    return pthread_mutexattr_destroy(attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_mutexattr_settype(void* attr, int type) {
    return pthread_mutexattr_settype(attr, type);
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

__attribute__((noinline))
static void fixture_once_initializer(void) {
    __asm__ __volatile__("" ::: "memory");
}

__attribute__((visibility("default"), noinline))
int fixture_pthread_once(void* once_control) {
    return pthread_once(once_control, fixture_once_initializer);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_rwlock_init(void* rwlock, const void* attr) {
    return pthread_rwlock_init(rwlock, attr);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_rwlock_destroy(void* rwlock) {
    return pthread_rwlock_destroy(rwlock);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_rwlock_rdlock(void* rwlock) {
    return pthread_rwlock_rdlock(rwlock);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_rwlock_wrlock(void* rwlock) {
    return pthread_rwlock_wrlock(rwlock);
}
__attribute__((visibility("default"), noinline))
int fixture_pthread_rwlock_unlock(void* rwlock) {
    return pthread_rwlock_unlock(rwlock);
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
