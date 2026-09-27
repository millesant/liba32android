# Shared ARM32 libm import evidence — 2026-09-27

## Supplied artifacts

- `libfmod.so` SHA-256:
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
- VLC Android APK SHA-256:
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

Bounded `readelf -WsW` inspection of the supplied FMOD ARM32 image and the
ARMv7 DSOs extracted from the supplied VLC APK gives this exact shared
undefined math-symbol intersection:

- `acos`
- `asin`
- `atan2`
- `cos`
- `cosf`
- `exp`
- `floor`
- `frexp`
- `ldexp`
- `log`
- `log10`
- `log10f`
- `pow`
- `powf`
- `sin`
- `sinf`
- `tan`

The VLC ARMv7 set has a broader math union, concentrated primarily in
`libvlc.so`; feature 047 intentionally starts with the target intersection
rather than claiming the full VLC-only surface.

## ABI boundary

The pinned Android NDK fixture is compiled for
`armv7a-linux-androideabi26` with `-mfloat-abi=softfp`. The generated
consumer therefore exercises the actual ARM32 base calling convention across
ordinary PLT/JUMP_SLOT calls into the generated compatibility `libm.so`.

The service translates only ABI representation and delegates numerical
calculation to host C++ math. It saves/restores host errno and floating-point
environment around each call so guest math does not leak state into the
embedding process.

## Limits

Static imports do not prove every function is reached on every application
path. Exact-value integration cases establish ABI/service wiring, not
bit-for-bit equivalence with every Android bionic architecture-specific libm
implementation or every exceptional/domain case.
