# Android shared-object __cxa_finalize lifecycle evidence — 2026-09-27

## Primary Android behavior

Android bionic's shared-object CRT defines a hidden destructor function
`__on_dlclose` that calls:

`__cxa_finalize(&__dso_handle)`

Source:
https://android.googlesource.com/platform/bionic/+/97d1c75ca5125f8e1dc6db32af1d22807fca1950/libc/arch-common/bionic/crtbegin_so.c

Bionic's `__cxa_finalize(dso)` walks atexit entries from newest to oldest,
selects exact DSO matches when dso is non-null, clears/extracts a selected
entry before invoking it to prevent recursive replay, and uses null dso for
process-wide finalization.

Source:
https://android.googlesource.com/platform/bionic/+/master/libc/bionic/atexit.cpp

The Android linker destroys a shared object by calling FINI_ARRAY in reverse
order and then DT_FINI.

Source:
https://android.googlesource.com/platform/bionic.git/+/master/linker/linker_soinfo.cpp

## Project implication

A real Android-compatible dlclose path cannot safely skip guest
`__cxa_finalize`: normal shared-object CRT code places the call in the ELF
destructor path. The bounded SVC boundary added by this follow-up provides the
guest-visible call target, while service-aware FINI execution and link-map
ownership remain separate.
