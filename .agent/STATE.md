# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-aeabi-dso-binding-association` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, and targeted final-close
`dlclose` unload.

Targeted final-close unload is DONE at
`742590cbc6740c9467e4b9d5f04884deeda16c1e`. The project operator confirmed all
required exact-head CI checks were green.

## Active post-roadmap follow-up

Lifecycle-scoped automatic DSO association is implemented.

ELF lifecycle execution can borrow a caller-owned current-object context. Every
guest lifecycle call scopes it to the call's stable graph object index and
restores the previous value on all exits; nested lifecycle execution restores
the outer provenance correctly.

`A32AeabiAtexitService` may borrow finite learned object-to-DSO binding
storage plus that context. Constructor-time non-zero DSO registrations learn
exact stable object associations. Identical associations reuse capacity;
conflicts or binding-capacity exhaustion return guest -1 atomically.
Context-free registrations preserve legacy behavior and do not invent
ownership.

The dynamic-open constructor path preserves the execution-context pointer while
overriding only trapped live r13, so constructor registrations can learn the
binding before handle publication.

Exact-object close consumes learned and/or caller-explicit bindings and rejects
disagreement. Successful targeted physical unload forgets learned associations
for reclaimed objects only after root release/unmapping succeeds; failed
teardown/reclamation preserves them for retry.

Focused regressions cover nested context restoration, real guest
`svc #0xD2` learning, identical reuse, conflicts, bounded capacity, context-free
legacy registration, binding-capacity reuse after retirement, dynamic-open
constructor learning, learned-only close, and explicit/learned disagreement.

Exact-head validation: NOT RUN.

## Deferred / partial

RTLD_LOCAL/RTLD_GLOBAL/RTLD_NODELETE/NOLOAD policy, RTLD_NEXT, lazy binding,
objects that never provide lifecycle-scoped `__aeabi_atexit` provenance,
process-wide exit ownership, Retired-slot compaction, concurrent graph
mutation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/TLS, concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate.
