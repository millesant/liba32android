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

Private common-synchronization SVC IDs continue from that range:

- 0x116 pthread_mutexattr_init
- 0x117 pthread_mutexattr_destroy
- 0x118 pthread_mutexattr_settype
- 0x119 pthread_once
- 0x11A internal pthread_once completion trampoline
- 0x11B pthread_rwlock_init
- 0x11C pthread_rwlock_destroy
- 0x11D pthread_rwlock_rdlock
- 0x11E pthread_rwlock_tryrdlock
- 0x11F pthread_rwlock_wrlock
- 0x120 pthread_rwlock_trywrlock
- 0x121 pthread_rwlock_unlock

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

## Mutex attributes and typed ownership

The supplied ARM32 FMOD/VLC/libc++ artifacts import
`pthread_mutexattr_init`, `pthread_mutexattr_settype`, and
`pthread_mutexattr_destroy`. The compatibility service therefore stores
finite opaque attribute metadata keyed by guest attr addresses and accepts
Android/Bionic type values NORMAL/DEFAULT=0, RECURSIVE=1, and ERRORCHECK=2.

`pthread_mutex_init` consumes the selected type when a bounded attr address
is supplied. Unknown mutex addresses used directly by lock/trylock retain the
accepted NORMAL static-initializer behavior.

Recursive mutexes track logical owner ID plus bounded acquisition depth. Owner
relock and trylock increment depth until the bounded Bionic-compatible ceiling;
overflow returns EAGAIN. Unlock decrements depth and only publishes a waiter
when the final acquisition is released.

Error-check mutex owner relock returns EDEADLK; owner trylock returns EBUSY.
For RECURSIVE and ERRORCHECK mutexes a non-owner unlock returns EPERM. NORMAL
mutex misuse remains application-undefined and preserves the pre-existing
compatibility behavior rather than inventing a new public error contract.

The supplied binaries do not import mutexattr getters, pshared, or protocol
operations, so those remain outside this slice.

## pthread_once execution and failure boundary

`pthread_once` state is finite metadata keyed by the opaque guest once-control
address. States are Uninitialized, Initializing, Done, and Failed.

The first logical caller becomes the initializer owner. At the pthread_once SVC
the service saves the post-SVC continuation, LR, and CPSR, then redirects guest
execution to the requested initializer. The initializer returns through one
caller-configured internal guest trampoline whose first instruction is private
SVC 0x11A. Completion marks the control Done, wakes every cooperative waiter,
and restores the original pthread_once continuation. This allows an initializer
that suspends in another supported pthread service to interleave with other
logical threads without host pthreads or a second scheduler.

Concurrent callers while Initializing suspend in the same bounded waiter table.
Calls after Done return immediately without rerunning the initializer. Nested
once initialization on the same logical thread is supported by choosing the
most recently started owned once-control at the completion trampoline.

POSIX does not provide a useful pthread_once error result for arbitrary guest
faults. If the initializer execution faults or otherwise terminates before the
completion trampoline, the embedding explicitly calls
`fail_once_initialization(thread_id)`. All Initializing controls owned by that
logical thread latch Failed. Subsequent calls fail the host-service execution
and existing waiters remain suspended because the owning guest execution is
already considered fatally failed; the compatibility layer does not fabricate
a POSIX success/error return or replay a partially run initializer.

## Read/write locks

The supplied VLC ARMv7 `libvlc.so` imports rwlock init/destroy/rdlock/wrlock/
unlock. The issue contract also keeps bounded tryrdlock/trywrlock as coherent
nonblocking companions; those two are covered by focused tests but are not
claimed as supplied-binary imports.

Rwlocks are finite opaque metadata: guest address, logical writer owner, reader
count, and waiters in the shared bounded waiter table. No host
`pthread_rwlock_t` or futex identity is mirrored.

The accepted default matches current Bionic's reader-preference behavior while
readers hold the lock: another reader may acquire even when a writer is already
pending. When the lock reaches fully unlocked state, pending writers are
preferred over pending readers; one writer is granted first, otherwise all
pending readers are granted together. This is a deterministic compatibility
rule, not a claim of global starvation freedom or scheduler fairness.

A writer-owner blocking rdlock/wrlock returns EDEADLK, while the corresponding
try operations return EBUSY. A writer unlock by another logical thread returns
EPERM. Read ownership is represented as a bounded aggregate count, matching the
observable Bionic implementation path rather than inventing per-reader host
identity. Destroy while owned or with pending readers/writers returns EBUSY.

Timed rwlocks and rwlock attrs are not imported by the supplied ARM32 evidence
and remain outside this slice despite the clock seam already existing for
condition variables.

## Scope limits

Process-shared synchronization, cond/rwlock attributes, mutex protocol/
pshared attributes, timed rwlocks, signals/futex internals, cancellation,
robust mutex recovery, priority inheritance/protection, and scheduler policy
remain outside this bounded synchronization/TLS service. Join/detach ownership and thread-exit destructor
iteration are handled by the separate pthread lifecycle service.
