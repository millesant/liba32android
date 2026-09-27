# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-targeted-dlclose-unload` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF FINI, linked
`__cxa_finalize`, resident exact-object final-close lifecycle, persistent
ownership/reachability planning, physical link-map reclamation, and dynamic
missing-object `dlopen` acquisition.

Dynamic acquisition is DONE at
`40479ffebe61c22c90d7d523292fc9e787be635d`.

## Active post-roadmap follow-up

Targeted final-close physical unload is implemented.

The ownership planner now supports a read-only exact-root-release query. It
shares ordinary validation/reachability logic, excludes only the selected root
from ownership, preserves caller live anchors, and mutates no graph/mapping
state.

Exact-object libdl teardown is now resumable without handle mutation. Complete
destructor state is idempotent on retry; Failed state remains non-replayable.

`A32LibDlUnloadTransaction` handles final synthetic references by preserving
all other live handles, requiring a clean current ownership baseline, planning
the exact post-root-release unreachable set, finalizing only that set in
requester-before-dependency order, then invoking accepted physical root
release/reclamation. The final synthetic handle is cleared only after physical
release succeeds.

Lifecycle or reclamation failure preserves the final handle/root. A later retry
skips objects already finalized successfully. Pre-existing unowned Active
objects block the transaction rather than being collected opportunistically.

`A32LibDlService` may optionally delegate dlclose to this path. Physical
service delegation requires `MappedGuestMemory`; legacy refcount-only and
exact-object close paths remain available when targeted unload is absent.

Focused regressions cover shared-root retention, live-handle retention,
service-level unload, requester-before-dependency failure/retry, orphan
rejection, non-final decrement, retirement, and physical unmapping.

Exact-head validation: NOT RUN.

## Deferred / partial

Automatic dynamic object-to-DSO-handle discovery, RTLD_NODELETE/global-group
policy, RTLD_GLOBAL/LOCAL flag expansion, RTLD_NEXT, lazy binding, Retired-slot
reuse/compaction, concurrent graph mutation, `DT_PREINIT_ARRAY`, process
argv/envp constructor ABI, broader pthread/TLS, concrete APK/ZIP byte
acquisition, higher-level public ELF/platform orchestration, JNI/graphics/audio
surfaces, and real Android device execution remain separate.
