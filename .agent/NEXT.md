# Next Work

The numbered 011-049 roadmap is COMPLETE.

The post-roadmap Android filesystem-library-source follow-up is DONE.

No acceptance gate is currently active.

## Ready follow-up candidates

- Persistent ELF lifecycle state plus legacy DT_INIT/DT_FINI.
- Broader pthread/thread creation/TLS services.
- Dynamic libdl acquisition/unload semantics.
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
