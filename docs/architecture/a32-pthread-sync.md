# A32 pthread mutex/semaphore synchronization service

Status: feature 045 implemented; exact-head validation pending

## Goal

Provide the smallest compatibility-side synchronization state machine that can
use feature 044's generic service suspension boundary without blocking a host
executor or inventing host-pointer identity for guest pthread objects.

The supplied ARM32 FMOD library and VLC ARMv7 set both import the default pthread
mutex operations and sem_init/sem_destroy/sem_wait/sem_post. Their broader
pthread surfaces remain separate.

## Guest/host protocol

Private SVC IDs:

- 0xB3 pthread_mutex_init
- 0xB4 pthread_mutex_destroy
- 0xB5 pthread_mutex_lock
- 0xB6 pthread_mutex_trylock
- 0xB7 pthread_mutex_unlock
- 0xB8 sem_init
- 0xB9 sem_destroy
- 0xBA sem_wait
- 0xBB sem_post

Guest synchronization pointers are logical 32-bit identity keys. The service
does not read or publish a bionic pthread_mutex_t/sem_t layout and never exposes
a host mutex/semaphore pointer.

## Scheduling handoff

The embedding sets one non-zero logical current-thread ID before guest
execution. Uncontended operations complete synchronously.

A contended mutex lock or zero-count sem_wait records a waiter in finite
caller-owned metadata and returns Suspended after setting the guest-visible
eventual return register. Unlock/post grants the oldest waiter first; mutex
ownership or semaphore grant is committed before the waiter is marked ready.
The embedding can then pop the ready record and resume the exact feature-044
post-SVC snapshot, so the blocking call is not executed twice.

## Scope limits

Mutex attrs/types, recursive/errorcheck mutexes, pthread_create/join/detach,
pthread_once, condition variables, rwlocks, TLS keys, pthread_self ABI,
process-shared semaphores, signals/futex internals, cancellation, robust mutexes,
and scheduler policy remain outside feature 045.
