# A32 pthread logical lifecycle service

Status: issue #39 implementation

## Goal

Provide the evidence-backed ARM32 pthread creation/identity/exit surface on top
of the existing logical A32 execution-context seam without creating host
threads or exposing host pthread objects.

The supplied ARM32 FMOD/VLC artifacts require pthread creation, identity,
basic creation attributes, and pthread_exit. See
[ARM32 pthread lifecycle import evidence](../research/evidence/arm32-pthread-lifecycle-imports-2026-10-01.md).

## Private service IDs

The lifecycle slice uses the next private service range after the accepted TLS
key services:

- `0x104` pthread_attr_init
- `0x105` pthread_attr_destroy
- `0x106` pthread_attr_getdetachstate
- `0x107` pthread_attr_setdetachstate
- `0x108` pthread_attr_getstacksize
- `0x109` pthread_attr_setstacksize
- `0x10A` pthread_create
- `0x10B` pthread_self
- `0x10C` pthread_equal
- `0x10D` pthread_exit

The guest partial-libc stubs remain direct `svc; bx lr` ARM functions.

## Identity model

`pthread_t` is one opaque non-zero 32-bit logical identity. Inside this
private compatibility implementation its numeric value is intentionally the
same as `runtime::A32LogicalThreadId`, so pthread synchronization/TLS and
lifecycle services can refer to one guest thread without a second identity
registry.

That numeric equivalence is not a public C ABI guarantee and never represents a
host thread ID or pointer.

`pthread_self` returns the current logical identity. `pthread_equal` is pure
identity equality and returns 1 or 0.

## Attribute model

Guest `pthread_attr_t*` values are treated as opaque logical addresses keyed
into finite caller-owned metadata; the service does not publish or depend on a
Bionic private struct layout.

The accepted subset is:

- init/destroy;
- get/set detach state;
- get/set stack size.

Initialization is joinable and uses the caller-configured default stack size.
Detach-state values follow Bionic's 0 = joinable and 1 = detached convention.
On 32-bit ARM the minimum accepted stack size is two configured guest pages.
Invalid state or sub-minimum stack size returns Android EINVAL.

Scheduling, guard, inherit-sched, explicit-stack, and other attributes are not
part of this slice.

## pthread_create

`pthread_create` never creates a host pthread.

The service allocates:

1. one finite caller-owned thread metadata slot;
2. one deterministic non-zero logical pthread/thread ID;
3. one page-aligned first-fit range from a caller-configured, already-mapped
   guest stack arena.

The service itself never maps memory.

The new `A32LogicalExecutionContext` begins at the exact guest start-routine
function pointer, passes the guest argument in r0, sets an 8-byte-aligned guest
SP at the top of the owned stack, and places the configured pthread_exit shim
address in LR.

The guest pthread output word is written only after all bounded identity/stack
validation succeeds. A failed guest output write commits no thread or stack
metadata and does not consume the next logical identity.

Finite thread or stack exhaustion returns Android EAGAIN.

## Start return and pthread_exit

A normal start-routine `bx lr` returns directly into the guest pthread_exit
shim with the start routine's r0 result unchanged.

`pthread_exit` records that 32-bit result in the current logical thread state,
marks the thread Exited, and returns the existing runtime
`A32HostServiceDisposition::Suspended`. This terminates that guest execution
at the scheduling handoff rather than executing the shim's trailing `bx lr`.

This slice records detached-at-create state but does not reclaim detached
threads or stacks. Join/detach ownership and reclamation are issue #40.

## Stack ownership

Only the caller-configured guest stack arena is allocated in this slice.
Allocation is deterministic first-fit against live lifecycle metadata, bounded
by the finite thread span, page-aligned, and non-overlapping.

Explicit caller-supplied stacks through pthread_attr_setstack/getstack are
deferred because the supplied ARM32 evidence does not require them.

## Boundaries

This service does not implement:

- pthread_join or pthread_detach;
- thread-exit TLS destructor iteration;
- detached resource reclamation;
- host pthread creation;
- condition variables, rwlocks, pthread_once, cancellation, or signals;
- JNI per-thread attachment/local-reference cleanup;
- public C API changes.

The existing synchronization/TLS-key service remains separate and keeps its
own mutex/semaphore/key policy while consuming the same logical thread identity.
