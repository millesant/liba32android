# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-android-filesystem-library-source` is IMPLEMENTED on
`bleeding`; exact-head required checks are its current acceptance gate.

Once terminal success is observed, close this follow-up. The next ready
production-readiness candidates are persistent ELF lifecycle state and legacy
DT_INIT/DT_FINI, broader pthread/TLS coverage, or a concrete APK/ZIP source.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
- Legacy DT_INIT/DT_FINI, __aeabi_atexit/static-destructor registration, and persistent lifecycle state.
- Broader pthread/thread creation/TLS services.
- Dynamic libdl acquisition/unload semantics.
- Broader/exceptional libm behavior.
- Concrete APK/ZIP byte acquisition and richer Android search policy.
- Higher-level public ELF/platform embedding APIs.

## Local-machine validation note

If a follow-up materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.
