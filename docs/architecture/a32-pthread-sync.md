# A32 pthread synchronization and TLS-key service

Status: mutex/semaphore feature 045 accepted; bounded TLS-key extension implemented

## Goal

Provide bounded compatibility-side pthread state machines without blocking a
host executor, inventing host-pointer identity for guest pthread objects, or
exposing host TLS.

The supplied ARM32 FMOD library and VLC ARMv7 set import the default pthread
mutex operations and sem_init/sem_destroy/sem_wait/sem_post. Supplied VLC ARMv7
`libmla.so` additionally imports `pthread_key_create`,
`pthread_key_delete`, `pthread_getspecific`, and `pthread_setspecific`.
Creation/identity/exit behavior is handled by the separate
[pthread lifecycle service](a32-pthread-lifecycle.md).

## Guest/host protocol

Private synchronization SVC IDs:

- 0xB3 pthread_mutex_init
- 0xB4 pthread_mutex_destroy
- 0xB5 pthread_mutex_lock
- 0xB6 pthread_mutex_trylock
- 0xB7 pthread_mutex_unlock
- 0xB8 sem_init
- 0xB9 sem_destroy
- 0xBA sem_wait
- 0xBB sem_post

Private TLS-key SVC IDs deliberately avoid the occupied libdl/libm/JNI service
ranges:

- 0x100 pthread_key_create
- 0x101 pthread_key_delete
- 0x102 pthread_getspecific
- 0x103 pthread_setspecific

Guest synchronization pointers and TLS keys are logical 32-bit identities. The
service does not read or publish bionic pthread object layouts and never exposes
host mutex, semaphore, pthread, or TLS pointers.

## Scheduling handoff

The embedding sets one non-zero logical current-thread ID before guest
execution. Uncontended operations complete synchronously.

A contended mutex lock or zero-count sem_wait records a waiter in finite
caller-owned metadata and returns Suspended after setting the guest-visible
eventual return register. Unlock/post grants the oldest waiter first; mutex
ownership or semaphore grant is committed before the waiter is marked ready.
The embedding can then pop the ready record and resume the exact feature-044
post-SVC snapshot, so the blocking call is not executed twice.

## Bounded TLS-key model

The service borrows finite caller-owned key metadata and finite per-thread
value metadata. `pthread_key_create` allocates a deterministic non-zero logical
key, writes it to guest memory, preserves the supplied guest destructor address
only as metadata, and returns Android EAGAIN when key capacity is exhausted.

`pthread_setspecific` updates or clears only the value for the selected
logical thread and key. Invalid/deleted keys return Android EINVAL; exhausting
the caller-owned value metadata returns Android ENOMEM.
`pthread_getspecific` returns the current logical-thread value or null,
including null for an invalid/deleted key.

`pthread_key_delete` invalidates the key and clears every stored value for it.
Deletion does not execute the guest destructor.

The reproducible partial ARM32 libc shim exports all four TLS-key functions as
direct private-SVC stubs. The base libc integration still executes its original
45-wrapper surface; a dedicated lifecycle consumer independently covers the
additional pthread lifecycle exports.

## Scope limits

Recursive/errorcheck mutexes, pthread_join/detach, pthread_once, condition
variables, rwlocks, process-shared semaphores, signals/futex internals,
cancellation, robust mutexes, scheduler policy, and thread-exit TLS destructor
iteration remain outside this bounded synchronization/TLS service.
