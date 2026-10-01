# ARM32 pthread TLS-key import evidence — 2026-10-01

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libmla.so` with
SHA-256
`4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`.

## Dynamic relocation evidence

Bounded `readelf -rW` inspection records four eager libc relocations:

- `0x007f81e0 R_ARM_JUMP_SLOT pthread_key_create@LIBC`;
- `0x007f81e4 R_ARM_JUMP_SLOT pthread_getspecific@LIBC`;
- `0x007f81ec R_ARM_JUMP_SLOT pthread_setspecific@LIBC`;
- `0x007f81f8 R_ARM_JUMP_SLOT pthread_key_delete@LIBC`.

The four symbols form one coherent TLS-key lifecycle and are absent from the
current partial libc shim at the validated starting revision.

## Boundary

This evidence establishes required symbol resolution for key creation,
deletion, current-thread value get, and current-thread value set. It does not
establish pthread creation/join/detach, host pthread identity, cancellation, or
thread-exit destructor iteration requirements.
