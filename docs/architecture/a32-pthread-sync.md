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

Private condition-variable SVC IDs extend the same synchronization service:

- 0x110 pthread_cond_init
- 0x111 pthread_cond_destroy
- 0x112 pthread_cond_wait
- 0x113 pthread_cond_timedwait
- 0x114 pthread_cond_signal
- 0x115 pthread_cond_broadcast

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

The synchronization/TLS service also exposes an internal exit-cleanup seam to
the pthread lifecycle layer. For one logical thread/key slot, lifecycle cleanup
may atomically take a non-null value together with its active nonzero destructor
metadata; taking clears the value before the guest callback. A destructor can
therefore observe null or repopulate the key through the normal getspecific /
setspecific services. After the finite lifecycle cleanup passes, remaining
values for that exiting logical thread are cleared.

This internal seam does not change pthread_key_delete: deleting a key still
invalidates it and clears stored values without executing any destructor.

The reproducible partial ARM32 libc shim exports all four TLS-key functions as
direct private-SVC stubs. The base libc integration still executes its original
45-wrapper surface; a dedicated lifecycle consumer independently covers the
additional pthread lifecycle exports.

## Condition variables

The supplied VLC ARMv7 set directly imports `pthread_cond_init/destroy`,
`pthread_cond_wait`, `pthread_cond_timedwait`, `pthread_cond_signal`, and
`pthread_cond_broadcast`. No supplied ARM32 artifact imports pthread cond
attributes, so this slice accepts only the default private/realtime condition
semantics and does not add condattr state.

A condition address is an opaque nonzero logical identity; the service does not
mirror Bionic's private pthread_cond_t word. Wait state is stored only in the
existing finite caller-owned waiter table.

`pthread_cond_wait` validates current ownership of the associated logical
mutex, reserves its waiter slot first, then releases the mutex through the same
internal mutex grant path used by pthread_mutex_unlock. This prevents a
capacity failure from releasing the mutex and makes the release/enqueue
transaction atomic at the host-service boundary.

A signal transfers the oldest condition waiter into the existing mutex
reacquisition queue. Broadcast transfers all current condition waiters in
deterministic arrival order. A transferred waiter is not published ready until
it owns the associated mutex; existing mutex waiters and condvar reacquirers
share one ordering/grant mechanism rather than separate lock models.

Bionic's current `pthread_cond_destroy` returns success without rejecting
active waiters; the compatibility service preserves that observable return and
does not invent an EBUSY rule for application-undefined concurrent destroy.

## Timed waits and deterministic clock seam

The ARM32 `timespec` consumed by `pthread_cond_timedwait` is the Bionic LP32
layout: signed 32-bit seconds followed by signed 32-bit nanoseconds. Nanoseconds
outside [0, 1_000_000_000) return Android EINVAL. Negative seconds return
ETIMEDOUT before mutex release, matching Bionic's `check_timespec` behavior.
A null timeout pointer is treated as an untimed wait, also matching the current
Bionic path.

Default condition timed waits use CLOCK_REALTIME. The compatibility service
does not read host wall time directly: an embedding-owned `A32PthreadClock`
returns logical nanoseconds. Tests inject a fake clock. The service exposes the
next absolute condition deadline and an explicit timeout-poll operation; no
unit test sleeps on wall clock time.

An expired waiter is moved into mutex reacquisition with eventual return code
ETIMEDOUT. It still cannot resume until the associated mutex is owned again.
The wake record therefore carries the eventual pthread return value for the
embedding to place into the saved post-SVC continuation before resume.

When a signal/broadcast call observes that the clock has reached an absolute
deadline, expired waiters are resolved first. This gives the bounded
single-threaded compatibility model one deterministic timeout-vs-signal race
rule and guarantees a waiter can transition only once.

The supplied VLC ARMv7 libraries also import `clock_gettime`; that libc
export is intentionally not implemented by this condvar slice. The internal
clock seam is sufficient to specify/test pthread timed-wait behavior while the
guest-visible clock API remains separate evidence-backed utility work.

## Scope limits

Recursive/errorcheck mutexes, pthread_once, rwlocks, process-shared
semaphores/condvars, cond attributes, signals/futex internals, cancellation,
robust mutexes, and scheduler policy remain outside this bounded
synchronization/TLS service. Join/detach ownership and thread-exit destructor
iteration are handled by the separate pthread lifecycle service.
