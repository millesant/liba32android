# ARM32 __cxa_atexit import evidence — 2026-10-03

## Inputs

Supplied evidence artifacts inspected locally:

- `libfmod.so` SHA-256
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`;
- `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk` SHA-256
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`.

The APK contains four ARMv7 native objects under `lib/armeabi-v7a/`:
`libc++_shared.so`, `libmla.so`, `libvlc.so`, and `libvlcjni.so`.

## Direct binary evidence

`readelf -sW libfmod.so` reports `__cxa_atexit` as an undefined global
function.

Bounded `readelf -rW` inspection of the four VLC ARMv7 objects reports eager
`R_ARM_JUMP_SLOT __cxa_atexit@LIBC` relocations at:

- `libc++_shared.so`: `0x00086f58`;
- `libmla.so`: `0x007f79bc`;
- `libvlc.so`: `0x025fb854`;
- `libvlcjni.so`: `0x00013d38`.

This establishes `__cxa_atexit` as a direct supplied-binary compatibility
requirement rather than a table-adjacent or speculative libc addition.

## Existing project seam

The accepted ARM EABI registration service already stores bounded
`(object, destructor, dso_handle)` records for
`__aeabi_atexit(object, destructor, dso_handle)`, and the existing
`__cxa_finalize` path consumes those same records.

The new guest `__cxa_atexit(destructor, object, dso_handle)` shim therefore
only needs to reorder the first two AAPCS32 words before trapping the existing
registration SVC. No new destructor ownership model or host pointer identity is
justified by this evidence.
