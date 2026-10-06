# ARM32 shared libm compatibility

Status: accepted current architecture; complete supplied VLC ARMv7 math-import surface implemented

## Scope

The partial guest `libm.so` now covers the complete 66-symbol math surface
selected from the supplied VLC `armeabi-v7a` native graph.

The original shared FMOD/VLC slice remains unchanged:

`acos, asin, atan2, cos, cosf, exp, floor, frexp, ldexp, log, log10, log10f,
pow, powf, sin, sinf, tan`.

The VLC completion adds exactly 49 directly evidenced math imports:

`acosf, atan, atan2f, atanf, cbrt, cbrtf, ceil, ceilf, cosh, exp2, exp2f,
expf, expm1, fabs, floorf, fmax, fmaxf, fminf, fmod, fmodf, frexpf, hypot,
hypotf, ldexpf, llrint, llrintf, llround, llroundf, log1p, logf, lrint,
lrintf, lround, lroundf, modf, modff, nanf, rint, rintf, round, roundf,
scalbn, sincos, sincosf, sinh, tanf, tanh, trunc, truncf`.

The evidence is pinned in
[`vlc-armv7-libm-imports-2026-10-06.md`](../research/evidence/vlc-armv7-libm-imports-2026-10-06.md).
This is complete only for the supplied VLC sample, not for every Bionic libm
export.

## Guest ABI

The original private SVC range `0xC1` through `0xD1` is preserved exactly.
The 49 VLC-only additions use the disjoint private range `0x180` through
`0x1B0`; no existing service IDs are renumbered.

The generated consumer and shim use Android ARMv7 softfp:

- float arguments/results occupy one core-register word;
- double arguments/results occupy little-endian core-register pairs;
- binary doubles use r0/r1 then r2/r3;
- `frexp(double, int*)` uses r2 for the logical exponent pointer while
  `frexpf(float, int*)` uses r1;
- `ldexp/scalbn(double, int)` use r2 for the signed exponent and
  `ldexpf(float, int)` uses r1;
- ARM32 `long` results from `lrint/lround` use signed r0, while
  `long long` results from `llrint/llround` use r0/r1;
- `modf/modff` publish the integral component through bounded logical guest
  pointers and return the fractional component normally;
- `sincos` receives sine/cosine output pointers in r2/r3 after the input
  double, while `sincosf` receives them in r1/r2;
- `nanf` reads its optional payload text through bounded `GuestMemory` using
  the caller-selected `A32LibmOptions::max_nan_tag_bytes` ceiling.

No host pointer is ever exposed to guest code.

## Host numerical engine

`A32LibmService` uses the corresponding host C++ math primitives as the
numerical engine. Every operation snapshots embedding-process `errno` and the
floating-point environment and restores both before returning.

The runtime still does not synthesize guest math errno or guest floating-point
exception flags. Rounding-sensitive functions such as `rint` and `lrint`
therefore do not create a new guest fenv model; exact fixture cases use values
whose expected result is independent of rounding mode.

`sincos/sincosf` compute the selected sine/cosine pair inside one preserved
host-math-state scope. `nanf` is the only selected call that consumes a guest
string and is separately bounded.

## Real ELF path

The pinned NDK fixture builds:

- generated ARM32 `libm.so` with all 66 direct SVC exports;
- a freestanding ARM32 consumer importing all 66 selected symbols.

Integration loads the consumer application-first and the shim through the
requester-aware namespace-gated platform catalog, resolves every selected name,
applies exactly 66 eager `R_ARM_JUMP_SLOT` relocations, and executes every
wrapper through the service registry.

Stable exact-value cases are preferred: zero/one identities, exact integer
powers/scales, 3-4-5 hypot, exact cbrt/fmod values, integral rounding inputs,
and binary-exact pointer-result cases cover every ABI shape. `nanf` is
validated by NaN classification rather than payload bits because NaN payload
encoding is implementation-specific.

## Limits

This does not claim complete Android/Bionic `libm.so`, vector/complex or
long-double coverage, bit-for-bit equivalence with architecture-specific Bionic
implementations, guest errno/fenv publication, or exceptional/out-of-range
integer-conversion equivalence. Additional math symbols require new binary
evidence rather than table adjacency.
