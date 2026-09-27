# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle,
`__aeabi_atexit` registration, registered finalization, and guest
`__cxa_finalize` service follow-ups are DONE.

`post-roadmap-a32-libc-cxa-service-aware-fini` is IMPLEMENTED on
`bleeding`; exact-head required checks are its current acceptance gate.

After terminal success, bind synthetic libdl handles and registered DSO handles
to stable link-map object indexes and build a bounded last-reference dlclose
transaction: service-aware FINI_ARRAY/DT_FINI first, ownership release second,
with mapping reclamation still deferred until reachability rules are explicit.

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
