# Android ARM __aeabi_atexit evidence — 2026-09-27

## Target evidence

The supplied FMOD ARM32 object imports `__aeabi_atexit`. Existing bounded
artifact evidence is recorded in
`docs/research/evidence/android17-aeabi-memory-helpers-2026-09-27.md`.

## Android/bionic behavior

The accepted Android baseline's ARM bionic implementation defines:

`__aeabi_atexit(object, destructor, dso_handle)`

as a direct delegation to:

`__cxa_atexit(destructor, object, dso_handle)`.

Bionic's atexit state stores the callback, callback argument, and DSO handle as
one registration record and returns zero on successful registration or -1 on
registration failure. Finalization is a separate operation.

Primary source references used for this bounded observable contract:

- https://android.googlesource.com/platform/bionic/+/c891e24/libc/arch-arm/bionic/__aeabi.c
- https://android.googlesource.com/platform/bionic/+/ebd90b9/libc/stdlib/atexit.c
- https://android.googlesource.com/platform/bionic/+/98384649b2d3f3eb5b03077bc0004e14a99a4d55/libc/stdlib/atexit.h

## Project boundary

The prepared post-roadmap slice copies only the observable registration ABI:
exact three guest words, ordered finite registration state, 0 success, and -1
capacity failure.

It does not copy bionic page allocation/locking internals, execute callbacks,
implement __cxa_finalize, infer DSO ownership, or perform unload.
