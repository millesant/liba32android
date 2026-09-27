# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-a32-cxa-finalize-service` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, bounded ARM EABI `__aeabi_atexit` registration,
and registered-destructor finalization.

## Active post-roadmap follow-up

Private SVC `0xD3` now provides a guest `__cxa_finalize` boundary over the
accepted registration/finalization state.

Guest r0 zero selects process-wide finalization; non-zero selects an exact
opaque DSO handle. Successful finalization resumes the guest void call; bounded
finalization failure is surfaced as service failure with detailed retained
result state.

A focused ARM service-registry regression executes SVC 0xD3 and verifies that a
matching registered destructor receives its exact object argument and becomes
Complete.

Exact-head validation: NOT RUN.

## Deferred / partial

Exporting `__cxa_finalize` from the partial libc shim, service-aware
FINI_ARRAY execution, DSO-handle/link-map ownership, dlclose/process-exit
transaction ordering, reference-counted unload and mapping reclamation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
dynamic missing-object libdl acquisition, concrete APK/ZIP byte acquisition,
higher-level public ELF/platform orchestration, JNI/graphics/audio surfaces,
and real Android device execution remain separate follow-up work.
