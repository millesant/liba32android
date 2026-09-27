# Android libc __errno contract — 2026-09-26

## Target evidence

The supplied ARM32 FMOD library and VLC ARMv7 `libvlc.so` both import
`__errno`.

## AOSP release baseline

The project compatibility baseline follows `android-latest-release`, which resolves to `android17-release` as of 2026-09-27. The exact Android 17.0.0 r1 bionic sources are used below.

## AOSP evidence

Android's public errno header defines `errno` as `(*__errno())` and declares
`int* __errno(void)`, documenting that it returns the calling thread's errno
storage.

Android 17 bionic returns `&__get_thread()->errno_value`, i.e. the calling thread's bionic errno storage rather than a process-global host errno value.

Primary sources:

- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/include/errno.h
- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/bionic/__errno.cpp

## Project consequence

Feature 037 models only the ABI-visible requirement: one caller-selected
logical guest address acts as the current guest execution/thread errno slot.

`A32LibcGuestErrnoState` implements both the feature-035 errno sink and an
exact `__errno` host service:

- sink publication writes a little-endian signed 32-bit errno value into the
  configured guest slot;
- `__errno` returns the slot's logical guest pointer in r0;
- the state owns no GuestMemory and does not use host process errno.

The embedding is responsible for providing a distinct state/slot per guest
thread when real guest threading is introduced.

## Limits

This does not implement Android TLS layout, `__get_tls`, pthread TLS keys, or
thread creation. It provides the observable `__errno` pointer/value seam only.


## Alignment result — 2026-09-27

Feature 037's one-logical-slot-per-guest-thread abstraction remains ABI-aligned:
guest code observes a stable `int*` for its current execution/thread context,
while the embedding chooses the backing guest slot. The project intentionally
does not mirror bionic's internal `pthread_internal_t` layout.
