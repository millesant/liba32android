# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-a32-aeabi-atexit-registration` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

The post-roadmap Android filesystem-library-source follow-up is accepted.

The post-roadmap persistent ELF lifecycle/legacy DT_INIT/DT_FINI follow-up is accepted.

## Active post-roadmap follow-up

Bounded ARM EABI `__aeabi_atexit` registration is integrated.

Private SVC `0xD2` records exact logical guest object, destructor, and DSO
handle values in finite caller-owned storage. Success returns zero; bounded
capacity exhaustion returns ARM32 -1 without mutating prior registrations.

The partial ARM32 libc shim/consumer now exposes/imports forty symbols, with
`__aeabi_atexit` as the new eager JUMP_SLOT path. Focused direct/ARM registry
tests and the real partial-libc integration are present.

Exact-head validation: NOT RUN.

## Deferred / partial

Registered-destructor finalization/`__cxa_finalize`, DSO-handle/link-map
ownership, process-exit and dlclose timing, mapping reclamation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/thread
creation/TLS services, dynamic missing-object libdl acquisition/unload,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate follow-up work.
