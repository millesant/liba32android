# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, bounded `__aeabi_atexit` registration,
registered-destructor finalization, guest `__cxa_finalize`, and linked
service-aware ELF FINI execution.

## Service-aware FINI validation

The final corrected revision
`0cd4e1ee1e9d08eb3bde9f50238fd61a5f6af5a6` passed all nine required
exact-head checks.

The accepted path now supports ordinary linked ARM32
`FINI_ARRAY -> __cxa_finalize -> SVC 0xD3 -> registered destructor -> FINI
resume`. Nested registered destructors use the trapped guest's live r13 so the
active FINI frame is preserved.

## Deferred / partial

DSO-handle/link-map ownership, last-reference dlclose transaction ordering,
reference-counted mapping reclamation, `DT_PREINIT_ARRAY`, process argv/envp
constructor ABI, broader pthread/TLS, dynamic missing-object libdl acquisition,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate.
