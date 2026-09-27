# Runtime spec delta — feature 044

A host service may return Suspended to yield the current A32 execution to an
external scheduler. The dispatch call returns successfully immediately after
the trapped SVC, identifies that SVC, preserves post-SVC registers/logical PC
and CPSR, and executes no later guest instruction.

Suspension consumes one finite service-call slot and does not roll back handler
side effects.

A helper may reconstruct a continuation only from suspended state. The caller
supplies a new non-zero finite instruction budget and optional stop target; the
helper preserves registers/CPSR, derives ARM/Thumb from CPSR, and resumes at the
already-advanced post-SVC PC.

No scheduler, pthread/semaphore ABI, futex, thread creation, TLS selection, or
synchronization-object layout is introduced.
