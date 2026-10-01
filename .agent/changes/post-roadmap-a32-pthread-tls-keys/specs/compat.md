# Compatibility spec delta — ARM32 pthread TLS keys

- Add private SVC IDs `0xBC` through `0xBF` for
  `pthread_key_create`, `pthread_key_delete`, `pthread_getspecific`, and
  `pthread_setspecific`.
- Borrow finite caller-owned key and per-thread value metadata.
- Allocate deterministic non-zero logical keys without mirroring Bionic's
  private key representation.
- Copy a non-null guest destructor function address into key metadata, but do
  not invoke it in this slice.
- Key creation writes one little-endian 32-bit key to guest memory and returns
  Android EAGAIN when key capacity is exhausted.
- TLS values are indexed by logical key plus caller-selected non-zero logical
  thread ID.
- `pthread_setspecific` updates/clears the current thread value, returning
  Android EINVAL for an invalid key and Android ENOMEM on bounded value
  exhaustion.
- `pthread_getspecific` returns the current thread value or null.
- `pthread_key_delete` removes all values for the key without calling a
  destructor and returns Android EINVAL for an invalid key.
- Extend the reproducible partial `libc.so` fixture and consumer with the four
  symbols and execute their real ARM32 wrapper path end-to-end.

Thread-exit destructor execution/iteration, pthread_create/join/detach/self,
cancellation, host TLS, and a full Bionic pthread ABI remain outside this
delta.
