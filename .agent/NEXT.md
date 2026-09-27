# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle,
`__aeabi_atexit` registration, and registered-destructor finalization
follow-ups are DONE.

No acceptance gate is currently active.

## Ready next follow-up

Bind resident libdl handles to stable link-map object indexes and implement a
bounded dlclose transaction for the last synthetic handle reference. The
transaction should compose registered finalization with persistent
FINI_ARRAY/DT_FINI execution while keeping actual mapping reclamation separate
until ownership/refcount rules are explicit.

## Other ready follow-up candidates

- Broader pthread/thread creation/TLS services.
- Dynamic missing-object libdl acquisition semantics.
- Concrete APK/ZIP byte acquisition and richer Android search policy.
- Higher-level public ELF/platform embedding APIs.
- Broader/exceptional libm behavior.

## Environment-blocked or decision-blocked work

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
- JNI/graphics/audio integration requires later application/device-facing work.

## Local-machine validation note

If a follow-up materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.
