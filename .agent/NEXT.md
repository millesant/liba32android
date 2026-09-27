# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle,
`__aeabi_atexit` registration, registered-destructor finalization, and guest
`__cxa_finalize` service follow-ups are DONE.

No acceptance gate is currently active.

## Ready next follow-up

Extend the generated partial libc with `__cxa_finalize` and make ELF
destructor execution service-aware so normal Android CRT FINI_ARRAY code can
invoke the accepted SVC finalizer.

After that, bind DSO handles to stable link-map objects and build the bounded
last-reference dlclose transaction before any unmapping work.

## Other ready follow-up candidates

- Broader pthread/thread creation/TLS services.
- Dynamic missing-object libdl acquisition semantics.
- Concrete APK/ZIP byte acquisition and richer Android search policy.
- Higher-level public ELF/platform embedding APIs.

## Environment-blocked or decision-blocked work

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
- JNI/graphics/audio integration requires later application/device-facing work.

## Local-machine validation note

If a follow-up materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.
