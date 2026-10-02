# ARM32 scheduler, priority, and affinity compatibility

This compatibility layer implements only the scheduler/priority helpers directly
evidenced by the supplied ARM32 FMOD/VLC binaries. Guest scheduler state stays
logical; no call mutates a host scheduler, host CPU affinity, process, or TID.

## Accepted surface

The partial libc shim exposes:

- `setpriority`
- `sched_get_priority_max`
- `sched_get_priority_min`
- `sched_getaffinity`
- `sched_setscheduler`
- `sched_yield`

Private SVC IDs `0x12c` through `0x131` dispatch these calls through
`A32SchedulerService`.

## Scheduler policies

The query side follows Linux/Bionic policy limits:

| Policy | min | max |
| --- | ---: | ---: |
| `SCHED_OTHER` | 0 | 0 |
| `SCHED_BATCH` | 0 | 0 |
| `SCHED_IDLE` | 0 | 0 |
| `SCHED_FIFO` | 1 | 99 |
| `SCHED_RR` | 1 | 99 |

Unknown policies fail with `EINVAL`.

Mutation is intentionally narrower. For `sched_setscheduler`, only pid 0
(the current logical guest thread) and `SCHED_OTHER` with priority 0 succeed,
updating the same `sched_policy` / `sched_priority` fields used by
`pthread_getschedparam` and `pthread_setschedparam`. Realtime policies and
the other Linux policies return `EPERM` rather than implying that the host
scheduler changed. Nonzero task IDs return `ESRCH`; negative task IDs return
`EINVAL`.

## Logical nice value

Linux exposes nice as a per-thread attribute in its pthread implementation.
`A32PthreadThreadState` therefore carries a logical `nice_value`, inherited
by new logical pthreads.

The evidenced FMOD call site executes `setpriority` only after establishing
`which == PRIO_PROCESS` and `who == 0`. That current-thread form is the only
accepted identity. Requested priorities are silently clamped to -20..19,
matching Linux. Increasing the numeric nice value (lower priority) updates
logical state; attempts to decrease it (raise priority without privilege)
return `EACCES`. Other selectors return `EINVAL` or `ESRCH`.

This logical nice value is compatibility metadata. It is never forwarded to a
host process or thread.

## ARM32 affinity ABI

Bionic LP32 defines `CPU_SETSIZE == 32` with 32-bit `unsigned long`, so the
accepted ARM32 `cpu_set_t` is exactly 4 bytes.

The cooperative runtime advertises exactly one synthetic logical CPU by default,
therefore `sched_getaffinity(0, 4, mask)` returns mask bit 0. The logical CPU
count is bounded to 1..32 through `A32SchedulerOptions`. This is not a query
of host CPU topology.

Only pid 0 and a 4-byte ARM32 CPU set are accepted. Nonzero task IDs return
`ESRCH`; malformed sizes return `EINVAL`; invalid guest pointers return
`EFAULT`.

## Cooperative yield

`sched_yield` writes success and returns the generic runtime
`A32HostServiceDisposition::Suspended`. The embedding resumes the saved
post-SVC continuation later, exactly like other cooperative blocking/yield
boundaries. No host `sched_yield` call occurs.

## Scope limits

This slice does not implement host CPU pinning, task-ID emulation, cgroups,
realtime scheduling, scheduler classes beyond logical query values, or
priority-based runnable-thread ordering in the embedding.
