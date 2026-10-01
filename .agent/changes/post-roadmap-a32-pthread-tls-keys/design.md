# Design — ARM32 pthread TLS keys

## Evidence boundary

The supplied VLC ARMv7 `libmla.so`
(SHA-256 `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`)
contains eager libc `R_ARM_JUMP_SLOT` imports for:

- `pthread_key_create`;
- `pthread_key_delete`;
- `pthread_getspecific`;
- `pthread_setspecific`.

These imports extend the already accepted pthread/semaphore evidence into one
coherent TLS-key family.

## Logical key model

The compatibility service borrows a bounded caller-owned key table. A free
slot receives a deterministic non-zero key value equal to its one-based slot
index. The guest destructor callback address is copied into metadata so later
thread-exit work can reason about it without publishing a host callback
identity.

`pthread_key_create` writes the 32-bit logical key to guest memory. Exhausted
key capacity returns Android EAGAIN.

## Per-thread value model

A second bounded table stores `(key, logical_thread_id, value)`. The existing
caller-selected non-zero logical thread ID is authoritative; no host
`pthread_t` or host TLS storage is exposed.

`pthread_setspecific` updates the current logical thread's value. A null value
removes an existing entry. Unknown/deleted keys return Android EINVAL and
metadata exhaustion returns Android ENOMEM.

`pthread_getspecific` returns the current logical thread's stored value or
null. Values do not leak across logical thread IDs.

`pthread_key_delete` removes the key and all associated bounded values.
POSIX key deletion does not invoke destructors, so this operation performs no
guest callback.

## Boundary

This slice deliberately does not implement thread-exit destructor iteration,
pthread_create/join/detach/self/equal, cancellation, host TLS, or general
scheduler/thread lifecycle.
