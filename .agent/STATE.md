# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, bounded ARM EABI `__aeabi_atexit` registration,
registered-destructor finalization, and the guest `__cxa_finalize` service
boundary.

## Post-roadmap __cxa_finalize validation

The guest finalization service passed all nine exact-head checks at
`fbd2e70c6b92bfe7b4242a7b69c083fdbf2de131`.

SVC `0xD3` accepts an exact opaque DSO selector (or zero for process-wide
finalization), delegates to bounded reverse-order registered destruction, and
surfaces bounded failure without host-pointer conversion.

## Deferred / partial

Exporting `__cxa_finalize` from the partial libc shim, service-aware
FINI_ARRAY execution, DSO-handle/link-map ownership, dlclose/process-exit
transaction ordering, reference-counted unload and mapping reclamation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
dynamic missing-object libdl acquisition, concrete APK/ZIP byte acquisition,
higher-level public ELF/platform orchestration, JNI/graphics/audio surfaces,
and real Android device execution remain separate follow-up work.
