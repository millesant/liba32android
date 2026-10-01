# NEXT

## Active pthread/TLS track

Continue `post-roadmap-a32-pthread-tls-keys` from
`2101b19bf9952172bc92f20245ae558f061657b6`.

The supplied VLC ARMv7 `libmla.so` directly requires four libc symbols through
eager R_ARM_JUMP_SLOT relocations:

- pthread_key_create;
- pthread_key_delete;
- pthread_getspecific;
- pthread_setspecific.

Implement one bounded logical TLS-key family in the existing pthread service.
Use caller-owned finite key/value metadata and the existing caller-selected
non-zero logical thread ID. Do not expose host pthread objects or host TLS.

Key creation may preserve a guest destructor callback address as metadata, but
thread-exit destructor execution/iteration is outside this slice. Keep
pthread_create/join/detach/self/equal, cancellation, and scheduler lifecycle
separate.

Extend the reproducible partial libc shim/consumer so all four symbols resolve
and execute end-to-end.

## Validation

Run exact-head checks after the focused host and real partial-libc regression
is committed. Escalate only failing checks. Leave CI immediately on terminal
success and converge.
