# Design — A32 service suspension boundary

## Disposition

Add `Suspended` to `A32HostServiceDisposition`. The dispatcher still invokes
handlers only after the existing service-ceiling preflight. A suspended service
counts as one successfully delivered service event, then returns immediately
without executing the instruction after SVC.

Suspension is not an error. The result records a dedicated suspension flag and
the exact SVC immediate while preserving the existing error/failing-SVC fields.

## State and continuation

The CPU already returns the post-SVC register array, logical PC, and CPSR used
for synchronous resume. Feature 044 exposes that same state as the suspension
snapshot.

`make_a32_service_resume_request` accepts only a suspended result plus a new
non-zero instruction budget and optional stop PC. It restores registers and
CPSR exactly, sets entry PC from returned r15, derives Thumb from CPSR T, and
does not rewind the SVC.

Each wake/resume is independently bounded; the runtime stores no hidden thread
or scheduler state.

## Synchronization boundary

The supplied targets share pthread mutex/semaphore/thread-creation imports, and
bionic uses waits when those operations cannot complete immediately. A future
compatibility layer may map that situation to `Suspended`, but object state,
wait queues, thread IDs, TLS, errno selection, futex semantics, and wake policy
remain embedding/compatibility work.
