# ARM32 pthread/semaphore scheduling-boundary evidence — 2026-09-27

## Supplied target imports

Bounded `readelf -WsW` inspection of the supplied ARM32 FMOD library and the
ARMv7 shared objects extracted from the supplied VLC Android APK found eighteen
pthread/semaphore imports shared by both target sets:

- `pthread_attr_destroy`
- `pthread_attr_init`
- `pthread_attr_setdetachstate`
- `pthread_attr_setstacksize`
- `pthread_create`
- `pthread_mutex_destroy`
- `pthread_mutex_init`
- `pthread_mutex_lock`
- `pthread_mutex_trylock`
- `pthread_mutex_unlock`
- `pthread_mutexattr_init`
- `pthread_mutexattr_settype`
- `pthread_once`
- `pthread_self`
- `sem_destroy`
- `sem_init`
- `sem_post`
- `sem_wait`

The VLC ARMv7 set additionally imports condition variables, rwlocks, join,
detach, TLS keys, scheduling parameters, pthread_exit, signal-mask handling, and
other pthread functions. This feature does not claim those APIs.

Artifact identities remain:

- `libfmod.so` SHA-256
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
- VLC APK SHA-256
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

## Android scheduling implication

Android bionic's pthread mutex and semaphore implementations have synchronous
uncontended paths but use futex-backed waiting when progress requires another
thread. pthread_create establishes a separate thread execution context. Those
observable requirements make a synchronous-only host-service dispatcher an
insufficient boundary for correct blocking compatibility.

Accepted Android baseline source paths:

- `libc/bionic/pthread_mutex.cpp`
- `libc/bionic/semaphore.cpp`
- `libc/bionic/pthread_create.cpp`
under `refs/heads/android17-release`.

Feature 044 therefore adds only a generic service suspension/resumption contract.
It deliberately does not copy bionic pthread object layouts, futex internals, or
host-thread implementation choices.

## Boundary

A future pthread/semaphore compatibility handler can complete an uncontended
operation synchronously or return the generic `Suspended` service disposition.
The embedding/scheduler then owns wait queues and wake ordering, and resumes the
same logical A32 state through a new finite execution budget.

Actual pthread/semaphore ABI services, object state, TLS selection, guest thread
creation, and wake policy remain later compatibility work.
