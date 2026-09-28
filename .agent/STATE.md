# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-arm32-libdl-load-policy` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, and lifecycle-scoped automatic DSO association.

Automatic DSO association is DONE at
`74b356f36d1cc2cd3a8326bc52b135e38e12e50a`. The project operator confirmed
all required exact-head CI checks were green.

## Active post-roadmap follow-up

ARM32 bionic libdl load policy is implemented.

The guest ABI now uses the historical LP32 dlfcn values:
`RTLD_NOW=0`, `RTLD_LAZY=1`, `RTLD_GLOBAL=2`, `RTLD_NOLOAD=4`,
`RTLD_NODELETE=0x1000`, `RTLD_DEFAULT=0xffffffff`, and
`RTLD_NEXT=0xfffffffe`. RTLD_LAZY remains ABI-compatible input only; relocation
stays eager.

Every successful named open owns one exact Active persistent root, including an
object previously present only as another root's dependency. Root policy
mutation is monotonic: Local never demotes Global and NODELETE never clears.

NOLOAD is resident-only and never invokes the dependency provider on a miss.
Global promotion deterministically publishes persistent global visibility.
Matching current bionic unload policy, linked Global roots and NODELETE roots
are retained on final close: the synthetic handle is cleared while lifecycle,
root ownership, mappings, object state, and learned DSO association remain
untouched. A Local NODELETE root remains non-global.

Policy publication happens only after an internal synthetic handle reference is
available. A failed policy mutation rolls that unexposed reference back.

RTLD_DEFAULT and RTLD_NEXT use their ARM32 sentinel values and cannot collide
with generated handles. RTLD_DEFAULT keeps the accepted first-root/global-root
search; RTLD_NEXT remains recognized but caller-relative lookup is deferred.

Focused regressions cover root creation/promotion, LP32 constants, resident and
dynamic NOLOAD, provider suppression, newly loaded Global roots, RTLD_DEFAULT /
RTLD_NEXT sentinel behavior, NODELETE close/reopen retention, and Global-only
retention in both exact and physical close paths.

Exact-head validation: NOT RUN.

## Deferred / partial

True lazy binding, caller-relative RTLD_NEXT, NODELETE/Global process-exit
teardown, DF_1_GLOBAL-specific unload retention beyond explicit libdl root
policy, concrete APK/ZIP byte acquisition, explicit-path dlopen and richer
Android pathname/namespace policy, Retired-slot compaction, concurrent graph
mutation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/TLS, higher-level public ELF/platform orchestration, JNI/graphics/audio
surfaces, and real Android device execution remain separate.
