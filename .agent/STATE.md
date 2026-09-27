# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none — `post-roadmap-dynamic-dlopen-acquisition` is DONE.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF FINI, linked
`__cxa_finalize`, resident last-reference `dlclose` lifecycle, persistent
ownership/reachability planning, physical link-map reclamation, and dynamic
missing-object `dlopen` acquisition.

Dynamic acquisition is accepted at
`40479ffebe61c22c90d7d523292fc9e787be635d`. The project operator confirmed
all required exact-head CI checks were green.

## Next post-roadmap direction

Implement targeted final-close unload lifecycle over only the objects that
become unreachable after releasing one dynamic ownership root.

The transaction must compute the post-release unreachable set before mutating
ownership, preserve every object still retained by another root or live handle,
execute registered/ELF teardown only for that newly unreachable set in
deterministic requester-before-dependency order, and only then release the root
and invoke physical reclamation.

Do not call the existing whole-root persistent destructor traversal for this
job: that would finalize shared dependencies that remain owned elsewhere.

## Deferred / partial

RTLD_NODELETE/global-group policy, RTLD_GLOBAL/LOCAL flag expansion, RTLD_NEXT,
lazy binding, Retired-slot reuse/compaction, concurrent graph mutation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate.
