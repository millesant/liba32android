# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source and persistent legacy lifecycle follow-ups are DONE.

`post-roadmap-a32-aeabi-atexit-registration` is IMPLEMENTED on `bleeding`;
exact-head required checks are its current acceptance gate.

After terminal success, close registration and implement bounded registered
destructor finalization / per-DSO reverse execution. That will be the lifecycle
substrate needed before real dlclose/unload ownership.

## Other ready follow-up candidates

- Broader pthread/thread creation/TLS services.
- Dynamic missing-object libdl acquisition/unload semantics.
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
