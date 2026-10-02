# A32 pthread logical lifecycle service

Status: bounded creation/identity/exit plus join/detach/thread-exit cleanup implemented

## Goal

Provide the evidence-backed ARM32 pthread lifecycle surface on top of the
existing logical A32 execution-context seam without creating host threads,
exposing host pthread objects, or adding a second runtime scheduler.

The supplied ARM32 FMOD/VLC artifacts require pthread creation and identity,
basic creation attributes, pthread_exit, and join/detach behavior. See
[ARM32 pthread lifecycle import evidence](../research/evidence/arm32-pthread-lifecycle-imports-2026-10-01.md).

## Private service IDs

The lifecycle service uses:

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
- `0x10E` pthread_join
- `0x10F` pthread_detach

The partial-libc functions remain direct `svc; bx lr` ARM stubs.

## Identity and attribute model

`pthread_t` is an opaque non-zero 32-bit logical identity. Inside the private
compatibility implementation its numeric value is intentionally the same as
`runtime::A32LogicalThreadId`, allowing lifecycle and synchronization/TLS
services to refer to the same guest thread. This equivalence is not a public C
ABI guarantee and is never a host thread ID or pointer.

`pthread_self` returns the current logical identity and `pthread_equal`
performs identity equality.

Guest `pthread_attr_t*` values are opaque logical addresses keyed into finite
caller-owned metadata. The accepted subset is init/destroy, detach-state
get/set, and stack-size get/set. Init is joinable with the caller-configured
default stack size. Detach state is 0 = joinable and 1 = detached. Stack sizes
below two configured guest pages and invalid detach states return Android
`EINVAL`.

## pthread_create and guest stack ownership

`pthread_create` never creates a host pthread. It transactionally allocates
one finite thread slot, one deterministic non-zero logical identity, and one
page-aligned first-fit region from a caller-configured already-mapped guest
stack arena. The compatibility service itself never maps memory.

The new `A32LogicalExecutionContext` starts at the exact guest start routine,
passes the argument in r0, uses an 8-byte-aligned guest SP, and places the
configured pthread_exit stub in LR. A normal start-routine return therefore
flows into pthread_exit with its r0 return value unchanged.

The pthread output word is published only after bounded identity/stack
validation succeeds. Failed publication consumes no thread/stack metadata or
logical identity. Finite thread or stack exhaustion returns Android `EAGAIN`.

## Join and detach ownership

The model follows the observed Bionic join-state behavior.

`pthread_join` returns `EDEADLK` for self-join, `ESRCH` for an unknown or
already-reclaimed target, and `EINVAL` for detached, already-claimed,
cleaning, or cleanup-failed targets.

Joining a live joinable target claims it exactly once, records the optional
return-value address, sets the eventual return code to zero, and returns
`Suspended`. No host wait occurs. After target cleanup completes, its return
value is published, the join wake becomes ready, and the embedding may pop the
wake. Popping reclaims the target metadata/owned stack before the joiner
resumes from the existing post-SVC continuation, so pthread_join is not
replayed.

Joining an already-exited joinable target completes synchronously, publishes
the optional return value, and reclaims the target exactly once.

`pthread_detach` returns `ESRCH` for an unknown/reclaimed target and
`EINVAL` for an already detached or join-claimed target. A running joinable
target becomes detached; an already-exited joinable target is reclaimed
immediately. A detached thread reclaims its owned metadata/stack as soon as
successful exit cleanup completes.

Reclamation makes the finite thread slot and first-fit stack range reusable.

## Thread-exit cleanup and TLS destructors

pthread_exit first stores the 32-bit return value and moves the logical thread
into a non-replayable Cleaning state.

When the lifecycle service borrows the pthread synchronization/TLS service, it
runs TLS destructors on the exiting logical thread and its live guest stack.
For each active key with a non-null value and nonzero guest destructor address,
the value is cleared before callback execution. The callback may therefore use
pthread_getspecific/setspecific through the borrowed synchronization/TLS
service and may repopulate a key.

The scan repeats for at most four destructor rounds, matching Bionic's
`PTHREAD_DESTRUCTOR_ITERATIONS` behavior. Values still populated after the
fourth pass are discarded with the exiting thread state. `pthread_key_delete`
never runs a destructor.

Each callback has explicit instruction and service-call ceilings and returns to
a caller-configured normalized guest stop PC. This slice makes pthread
synchronization/TLS services available during the callback; it does not imply
that arbitrary Android/JNI services are nested automatically.

After TLS cleanup, an optional `A32PthreadThreadExitHook` runs. It is an
explicit compatibility-layer seam for future per-thread JNI/local-reference
cleanup and contains no JNI semantics itself.

Only after successful cleanup does the thread become Exited, wake a claimed
joiner, or reclaim itself if detached. Cleanup faults, invalid destructor
addresses, service failures/suspension, instruction exhaustion, exit-hook
failure, or a late join-result write failure latch `CleanupFailed`; the
pthread_exit cleanup transaction is not replayed.

## ARM32 integration

The dedicated lifecycle consumer imports all twelve accepted lifecycle symbols
through eager `R_ARM_JUMP_SLOT` relocations. Integration exercises:

- attrs plus logical self/equality;
- a live pthread_join that suspends;
- target return through pthread_exit;
- return-value publication, wake, reclamation, and post-SVC join resume;
- pthread_detach of a running target followed by immediate reclamation at exit.

A focused host/ARM execution regression additionally proves four-pass TLS
destructor repopulation and cleanup-failure latching.

## Boundaries

This service does not implement cancellation/cleanup handlers, signals,
condition variables, rwlocks, pthread_once, robust/process-shared
synchronization, scheduler-priority policy, host pthread lifecycle, or actual
JNI per-thread cleanup. Explicit caller-supplied stacks and broader pthread
attributes also remain evidence-driven follow-up work.
