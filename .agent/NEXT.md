# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle, and
`__aeabi_atexit` registration follow-ups are DONE.

`post-roadmap-a32-registered-destructor-finalization` is IMPLEMENTED on
`bleeding`; exact-head required checks are its current acceptance gate.

After terminal success, the next lifecycle step is DSO-handle/link-map binding
plus a bounded dlclose transaction that composes registered finalization,
FINI_ARRAY/DT_FINI, reference ownership, and only then eventual mapping
reclamation.

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
