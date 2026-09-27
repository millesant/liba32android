# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle,
`__aeabi_atexit` registration, and registered-destructor finalization
follow-ups are DONE.

`post-roadmap-a32-cxa-finalize-service` is IMPLEMENTED on `bleeding`;
exact-head required checks are its current acceptance gate.

After terminal success, extend the generated partial libc with
`__cxa_finalize` and make ELF destructor execution service-aware. That allows
normal Android CRT FINI_ARRAY code to trigger the accepted registered
destructor state before a bounded dlclose ownership transaction is added.

## Other ready follow-up candidates

- DSO-handle/link-map binding and dlclose ownership after service-aware FINI.
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
