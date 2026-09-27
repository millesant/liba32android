# Android libc __errno contract — 2026-09-26

## Target evidence

The supplied ARM32 FMOD library and VLC ARMv7 `libvlc.so` both import
`__errno`.

## AOSP evidence

Android's public errno header defines `errno` as `(*__errno())` and declares
`int* __errno(void)`, documenting that it returns the calling thread's errno
storage.

Bionic's implementation returns a pointer into thread-local storage rather than
a process-global host errno value.

Primary sources:

- https://android.googlesource.com/platform/bionic/+/main/libc/include/errno.h
- https://android.googlesource.com/platform/bionic/+/main/libc/bionic/__errno.cpp

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
