# Supplied VLC ARMv7 libm import evidence — 2026-10-06

## Input

- VLC Android APK:
  `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
- APK SHA-256:
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`
- extracted `lib/armeabi-v7a/libvlc.so` SHA-256:
  `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`

The APK's four ARMv7 DSOs were inspected with bounded `readelf -sW` symbol
enumeration. Math-name classification was cross-checked against Android
Bionic's libm export map; `ldexp` remains included because it is already an
accepted supplied-target math seam in L32-C022 and is directly undefined by
the supplied `libvlc.so`.

Primary Bionic export reference:
`https://android.googlesource.com/platform/bionic/+/master/libm/libm.map.txt`

## Exact supplied VLC set

Only `libvlc.so` adds undefined math imports in this supplied
`armeabi-v7a` graph. The selected union is exactly 66 names:

`acos, acosf, asin, atan, atan2, atan2f, atanf, cbrt, cbrtf, ceil, ceilf,
cos, cosf, cosh, exp, exp2, exp2f, expf, expm1, fabs, floor, floorf, fmax,
fmaxf, fminf, fmod, fmodf, frexp, frexpf, hypot, hypotf, ldexp, ldexpf,
llrint, llrintf, llround, llroundf, log, log10, log10f, log1p, logf, lrint,
lrintf, lround, lroundf, modf, modff, nanf, pow, powf, rint, rintf, round,
roundf, scalbn, sin, sincos, sincosf, sinf, sinh, tan, tanf, tanh, trunc,
truncf`.

The previously accepted shared FMOD/VLC set is 17 names. Subtracting it leaves
the exact 49-symbol completion list recorded by L32-C074 and issue #73.

## ABI shapes selected by the evidence

The set exercises more than unary/binary floating returns:

- double and float unary/binary functions;
- double/float plus signed integer exponents;
- `int*`, `double*`, and `float*` result pointers;
- ARM32 `long` and `long long` integer returns;
- paired `sincos/sincosf` pointer outputs;
- the guest C-string input to `nanf`.

The pinned fixture therefore has to exercise each of those AAPCS32 softfp
shapes rather than proving coverage only by symbol presence.

## Limits

Static undefined imports show linkage requirements, not that every function is
reached in every VLC session. The evidence does not justify unimported libm
symbols, vector/complex/long-double families, bit-for-bit Bionic numerical
equivalence, or a guest floating-point environment model.
