# Android 17 malloc/calloc/realloc/free contract — 2026-09-26

## Target evidence

The supplied ARM32 FMOD library and VLC ARMv7 `libvlc.so` both import the
allocator quartet `malloc`, `calloc`, `realloc`, and `free`.

## Android 17 baseline

Current compatibility research follows `android-latest-release`, resolved in
feature 039 to the Android 17 release line. Exact allocator evidence is taken
from Android 17.0.0 r1 bionic/Scudo source.

Bionic's public malloc header documents malloc/calloc/realloc failure as a null
pointer with errno set. The Android 17 Scudo C wrappers provide the concrete
default allocator behavior relevant to this bounded compatibility slice:

- `malloc(size)` delegates to the allocator and applies errno on a null result;
- `calloc(n,size)` checks multiplication overflow, uses ENOMEM on overflow,
  requests zero-initialized allocation, and applies errno on allocation failure;
- `realloc(nullptr,size)` uses the malloc allocation path;
- `realloc(ptr,0)` deallocates the live allocation and returns null;
- failed non-zero realloc leaves the old allocation live;
- `free(ptr)` delegates to allocator deallocation;
- Android's Scudo build forces a 16-byte allocator minimum alignment even on
  32-bit targets.

Primary sources:

- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/include/malloc.h
- https://android.googlesource.com/platform/external/scudo/+/refs/tags/android-17.0.0_r1/standalone/wrappers_c.inc
- https://android.googlesource.com/platform/external/scudo/+/refs/tags/android-17.0.0_r1/Android.bp
- https://android.googlesource.com/platform/external/scudo/+/refs/tags/android-17.0.0_r1/standalone/combined.h

## Project model

Feature 040 reproduces these observable ABI decisions with a bounded
caller-provided guest arena; it does not copy Scudo internals.

The arena is already mapped read/write by the embedding. Allocation metadata is
caller-owned finite storage. Guest pointers are aligned logical 32-bit addresses
inside that arena.

Zero-size malloc/calloc requests receive the same minimum internal allocation
shape as ordinary allocation while preserving their requested byte count as
zero. Non-null realloc with size zero frees and returns null.

Allocation/calloc-overflow/realloc-growth exhaustion returns null and publishes
Android ENOMEM (12) through the existing guest errno sink. Invalid non-null
free/realloc pointers are treated as runtime `Failed` because the current
runtime has no guest allocator-fatal-signal seam; they are never silently
accepted.

## Limits

The first guest heap is single-context and externally serialized. It owns no
mappings, does not return memory to the host OS, does not reproduce Scudo
quarantine/tagging/debug hooks, and does not implement malloc_usable_size,
aligned allocation, mallinfo/mallopt, or C++ allocation operators.
