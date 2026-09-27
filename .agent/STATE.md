# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-dynamic-dlopen-acquisition` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF FINI, linked
`__cxa_finalize`, bounded resident last-reference `dlclose` lifecycle,
persistent ownership/reachability planning, and physical persistent link-map
reclamation.

The reclamation transaction is DONE. Its validated result revision is
`33037680cdf3dd25a7b60dc12b051fef5cfebf98`.

## Active post-roadmap follow-up

Dynamic missing-object libdl acquisition is implemented.

`A32LibDlService` may optionally delegate named dlopen to
`A32LibDlOpenTransaction`. Without that transaction, the accepted
resident-only behavior remains available.

The open transaction considers Active slots only, rejects failed-constructor or
non-Pending-destructor resident state, resolves missing roots through the
caller-owned dependency-provider seam, appends/reuses Local persistent roots,
eagerly relocates newly appended objects with the current global scope, seals
their GNU RELRO, and runs persistent constructors dependency-first.

Synchronous guest dlopen passes trapped live r13 into constructor execution so
nested guest lifecycle calls stay below the active caller frame.

A genuinely new load preflights handle capacity before graph mutation. Failures
before constructor execution remove a root added by the attempt and physically
reclaim newly unreachable Pending/Pending mappings while preserving all live
handle anchors. Constructor-stage failure latches Failed and remains resident;
later dlopen refuses replay.

Handle publication occurs only after successful initialization. Repeated opens
reuse/refcount the same object handle. Retired tombstones are excluded from
resident name lookup, handle-root symbol lookup, and dladdr address matching.

Focused regressions are integrated in
`tests/compat/a32_libdl_open_transaction.cpp`.

Exact-head validation: NOT RUN.

## Deferred / partial

Recursive final-close lifecycle over only newly unreachable objects,
RTLD_NODELETE/global-group policy, RTLD_GLOBAL/LOCAL flag expansion, lazy
binding, RTLD_NEXT, Retired-slot reuse/compaction, concurrent graph mutation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate.
