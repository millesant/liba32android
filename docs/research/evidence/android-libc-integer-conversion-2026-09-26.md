# Android libc integer conversion contract — 2026-09-26

## Target evidence

The supplied ARM32 FMOD library and VLC ARMv7 `libvlc.so` both import
`atoi` and `strtol`; see the corrected common-symbol evidence in
`shared-libc-memory-string-imports-2026-09-26.md`.

## AOSP release baseline

The project compatibility baseline now follows the official `android-latest-release` manifest. As of 2026-09-27 that manifest resolves to `android17-release`; the stable Android 17.0.0 r1 bionic tag is used below for exact source evidence.

## AOSP evidence

Android 17 bionic implements `atoi(s)` by calling
`strtol(s, nullptr, 10)`.

Its signed conversion implementation:

- accepts base 0 or bases 2 through 36;
- rejects negative bases, base 1, and bases above 36 with return 0,
  `errno = EINVAL`, and end pointer equal to the original input;
- skips leading C whitespace and one optional sign;
- recognizes guarded `0x`/ `0X` hexadecimal prefixes;
- recognizes guarded `0b`/ `0B` binary prefixes for base 0 or 2;
- uses leading zero for octal when base is 0 and decimal otherwise;
- returns the original input as end pointer when no digits are consumed;
- continues consuming valid digits after overflow so the end pointer remains
  correct, then sets `errno = ERANGE` and returns the signed limit.

Primary source:

https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/bionic/strtol.cpp

## Project boundary

ARM32 Android is ILP32, so the compatibility service models `long` and
`int` as signed 32-bit results in r0. The guest string remains a logical
GuestMemory address. For `strtol`, r1 is a logical guest address of a
32-bit end-pointer slot (or zero) and r2 is the signed base argument.

The service uses ASCII byte classification for the C whitespace/digit/letter
set and a caller-supplied maximum number of guest bytes examined. If bounded
GuestMemory cannot supply enough bytes to determine the conversion/end pointer,
the host service returns `Failed` rather than reading without limit.

A caller-owned errno sink receives Android guest errno numbers 22 (EINVAL) and
34 (ERANGE). The service does not write host process errno.

## Limits

This evidence covers `atoi` and signed 32-bit `strtol` only. It does not
cover locale-specific classification, unsigned/wide/64-bit conversions,
`strtod`, or a guest `__errno`/TLS implementation.


## Alignment result — 2026-09-27

The prepared feature-035 parser was re-audited against Android 17's exact
`strtol.cpp` source. Its accepted base range, guarded 0x/0b prefixes,
whitespace/sign handling, no-digit end pointer, overflow digit consumption,
EINVAL/ERANGE cases, and ARM32 signed-result model match the release behavior.
The project's caller-selected byte ceiling remains an intentional safety bound,
not an Android semantic claim.
