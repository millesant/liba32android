# ARM32 signal compatibility

The signal compatibility layer is a bounded logical model for the supplied
AArch32 binaries. It does not forward guest signals to host processes or
threads, and it does not translate guest pthread identities into host TIDs.

## Accepted surface

The supplied ARM32 evidence requires exactly these libc entry points:

- `raise`
- `sigaction`
- `sigpending`
- `pthread_sigmask`
- `sigwait`

Private SVC IDs `0x127` through `0x12b` route those calls to
`A32SignalService`.

The ARM32 ABI uses a 32-bit `sigset_t`. This layer exposes public signal
numbers 1 through 31; bit 31 of the 32-bit set (signal 32) is treated as
Bionic-internal/reserved rather than advertising realtime-signal support.
`SIGKILL` (9) and `SIGSTOP` (19) are always filtered out of block masks.

For ARM32, the guest `struct sigaction` is exactly 16 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | handler / sigaction function value |
| 4 | 4 | `sigset_t sa_mask` |
| 8 | 4 | `int sa_flags` |
| 12 | 4 | restorer function value |

All addresses and function values remain 32-bit guest values.

## Logical state

Signal dispositions are process-level caller-owned runtime state. Each existing
`A32PthreadThreadState` carries its own 32-bit signal mask and thread-pending
set, so pthread creation/reclamation remains the single owner of logical-thread
lifetime. New logical pthreads inherit the creator's mask and begin with no
thread-pending signals.

A separate bounded waiter span records suspended `sigwait` calls. Waiters use
the same cooperative service boundary as mutexes, condition variables, and
joins: `sigwait` returns `Suspended`, a later logical signal marks the oldest
matching waiter ready, and the embedding resumes that thread after the trapped
SVC. No host thread is blocked.

A process-pending bitset exists only inside the signal service. The
`queue_process_signal` method is an embedding/test seam for logical signal
generation; it is not host signal passthrough.

## Delivery boundaries

The implemented successful path is the one directly exercised by supplied VLC:
block `SIGPIPE`, generate it with `raise`, observe it with `sigpending`,
consume it with `sigwait`, then restore the old mask.

`raise` targets the current logical guest thread, matching Bionic's
thread-directed behavior without using host `tgkill`. A masked signal becomes
thread-pending. Ignored/default-ignored signals are discarded.

This slice does not claim general asynchronous guest handler delivery.
Unblocked signals that would invoke a custom handler, terminate the process,
stop the process, or continue a stopped process cross an explicit service
failure boundary with a recorded `A32SignalBoundaryError`. In particular,
the supplied VLC code contains `raise(SIGFPE)` fatal paths; those do not return
fake success.

Changing a mask can expose pending signals. Ignored pending signals are
discarded; any remaining signal requiring real delivery crosses the same
explicit boundary instead of being silently left in an impossible state.

## Error behavior

`sigaction`, `sigpending`, and `raise` follow libc-style `0/-1` returns
and publish Android guest `errno` through the existing
`A32LibcErrnoSink`. Invalid signal arguments use `EINVAL`; invalid guest
pointers use `EFAULT`.

`pthread_sigmask` and `sigwait` return Android error numbers directly and
do not modify guest errno, matching Bionic's pthread-style API boundary.
Invalid mask operations return `EINVAL`; invalid guest pointers return
`EFAULT`.

Waiter capacity is finite. Exhaustion is a deterministic host-service failure,
not an invented POSIX error.

## Scope limits

This contract does not implement `pthread_kill`, full Linux signal delivery,
signal frames, `sigaltstack`, realtime signals, host-signal passthrough, or
arbitrary asynchronous guest handlers. Those require separate evidence and an
explicit execution-context design.
