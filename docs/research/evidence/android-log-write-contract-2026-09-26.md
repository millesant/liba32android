# Android `__android_log_write` API / AAPCS32 evidence — 2026-09-26

## Question

What is the smallest authoritative guest ABI contract required to bridge the
shared `__android_log_write` import observed in the supplied ARM32 FMOD/VLC
artifacts?

## Android release baseline

The compatibility baseline follows the official `android-latest-release` manifest, which resolves to `android17-release` as of 2026-09-27. The system/logging project is part of that manifest. The log-write ABI has remained stable across the historical Android 9 source and current system/logging API inspected for this contract.

## Android API evidence

Current AOSP `liblog/include/android/log.h` declares:

`int __android_log_write(int prio, const char* tag, const char* text);`

The same three-argument C declaration is present in the Android 9 AOSP
`android/log.h` tag inspected for historical stability.

Current AOSP documentation states that the function writes the constant string
`text` with priority `prio` and tag `tag`, returning 1 when written or
`-EPERM` when logging policy rejects it.

Current AOSP `logger_write.cpp` routes `__android_log_write` through
`__android_log_buf_write`; the logger path substitutes default-tag policy when
the tag pointer is null. This supports preserving a guest null tag as a distinct
value rather than fabricating an empty string in the compatibility bridge.

Primary sources:

- AOSP current header:
  https://android.googlesource.com/platform/system/logging/+/refs/heads/main/liblog/include/android/log.h
- AOSP current implementation:
  https://android.googlesource.com/platform/system/logging/+/refs/heads/master/liblog/logger_write.cpp
- AOSP Android 9 header:
  https://android.googlesource.com/platform/system/core/+/refs/tags/android-9.0.0_r55/liblog/include/android/log.h

## AAPCS32 evidence

Arm's current AAPCS32 specifies r0-r3 as the first four argument/scratch
registers and states that the first four registers are used to pass argument
values and return a function result. For this function's three 32-bit machine
arguments, the bridge therefore consumes:

- r0: `prio`;
- r1: guest pointer to `tag`;
- r2: guest pointer to `text`;

and writes the 32-bit result to r0.

Primary source:

- Arm ABI repository, `aapcs32/aapcs32.rst`:
  https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst

## Implementation consequence

A first bridge does not need stack or variadic marshalling. It does need
bounded logical-guest C-string reads, exact preservation of signed 32-bit
priority/result bit patterns, null-tag representation, and an explicit failure
path before host effects when guest strings cannot be read safely.

The bridge should remain sink-based rather than directly calling host
`liblog`; host logging/filter policy is a separate embedding decision.

## Limits

This evidence defines the call shape, not a complete `liblog.so`
compatibility library. It does not justify implementing
`__android_log_print` or `__android_log_vprint`, does not choose a private
SVC number, and does not solve guest ELF symbol export/provider selection.


## Alignment result — 2026-09-27

No feature-026 ABI change is required by the Android 17 baseline: the bridge
continues to model the same three machine-word arguments and signed 32-bit
result, preserves a null tag for downstream policy, and keeps host logging
policy outside the game-agnostic runtime.
