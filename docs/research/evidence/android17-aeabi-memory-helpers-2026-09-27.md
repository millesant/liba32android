# Android ARM EABI memory-helper evidence — 2026-09-27

## Target artifact evidence

Bounded static inspection used the supplied artifacts without vendoring them.

- `libfmod.so` SHA-256:
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
- VLC Android APK SHA-256:
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

The supplied FMOD ARM32 object imports plain `memmove` from its libc surface.
The four extracted VLC `armeabi-v7a` DSOs collectively import plain
`memmove` plus exactly these twelve ARM EABI memory helpers:

- `__aeabi_memcpy`, `__aeabi_memcpy4`, `__aeabi_memcpy8`
- `__aeabi_memmove`, `__aeabi_memmove4`, `__aeabi_memmove8`
- `__aeabi_memset`, `__aeabi_memset4`, `__aeabi_memset8`
- `__aeabi_memclr`, `__aeabi_memclr4`, `__aeabi_memclr8`

The VLC member hashes observed for this scan were:

- `libc++_shared.so`:
  `30986ee10a51d9d9486d51d2a8b152d86b6f9c818f4e029a238b30924939b57d`
- `libmla.so`:
  `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`
- `libvlc.so`:
  `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`
- `libvlcjni.so`:
  `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`

FMOD also imports `__aeabi_atexit`; that symbol is intentionally excluded from
this memory-helper slice because it registers static C++ destructors rather than
performing a stateless memory operation.

## Android bionic behavior

The official bionic ARM source at
`libc/arch-arm/bionic/__aeabi.c` implements the memcpy and memmove alignment
variants by delegating to the corresponding libc primitive. The memset helpers
use ARM EABI argument order `(dest, n, c)`, explicitly reversing the second
and third arguments relative to libc `memset(dest, c, n)`. The memclr helpers
delegate to the matching memset helper with byte value zero.

The bionic ARM symbol map exposes these EABI helper names as libc symbols. The
accepted project Android baseline remains `android17-release`; current bionic
source retains the same observable helper contract.

Primary source paths:

- https://android.googlesource.com/platform/bionic/+/refs/heads/android17-release/libc/arch-arm/bionic/__aeabi.c
- https://android.googlesource.com/platform/bionic/+/refs/heads/android17-release/libc/libc.arm.map

## Feature boundary

Feature 043 copies only these observable ABI relationships into the bounded
compatibility layer. It does not copy bionic implementation internals, claim
alignment-specific acceleration, or add `__aeabi_atexit`/C++ destructor
registration.
