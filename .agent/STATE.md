# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-link-map-reclamation-transaction` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF FINI, linked
`__cxa_finalize`, bounded resident last-reference `dlclose` lifecycle, and
persistent link-map reclamation planning.

The reclamation planner is DONE. Its validated result revision is
`8093ad7cc7ed83f97202eb87ff1da375406e6a9e`.

## Active post-roadmap follow-up

Persistent link-map reclamation mutation is implemented.

Accumulated object slots are now explicitly Active or Retired. Retirement keeps
the stable graph index as a tombstone; append/planning/reclamation ignore Retired
slots for active identity reuse, ownership roots, global visibility, and live
anchors. Reloading the same identity after retirement allocates a fresh higher
index rather than reviving an unmapped slot.

`release_elf32_link_map_root` removes one exact persistent root from liveness
and reclaims newly unreachable active objects. A separate
`reclaim_elf32_link_map_unreachable` sweep handles the case where a root was
previously released while an external owner kept its closure alive and that
non-root owner later disappears.

Physical reclamation requires lifecycle state for every accumulated slot.
Candidates are reclaimable only when never constructed (Pending/Pending) or
fully torn down (Complete/Complete). Failed or partial lifecycle state blocks
the transaction before unmapping.

All reclaimable PT_LOAD mappings are validated and snapshotted before the first
unmap under caller object/segment/byte ceilings. Snapshots preserve current
guest bytes and per-page permissions. Unmap failure restores touched mappings;
rollback failure is explicit. Root/global/tombstone publication occurs only
after all unmaps succeed.

Focused regressions cover successful physical unmapping, shared-root retention,
live-anchor retention followed by later sweep, global pruning, both accepted
lifecycle states, partial-lifecycle rejection, resource-limit preservation, and
same-identity reload into a new stable slot.

The concrete memory backend has no deterministic syscall-failure injection seam,
so the rollback implementation is not claimed as a forced unit-test outcome.

Exact-head validation: NOT RUN.

## Deferred / partial

Destructor execution inside the reclamation transaction, RTLD_NODELETE policy,
Retired-slot reuse/compaction, concurrent graph mutation, dynamic missing-object
dlopen, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/TLS, concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate.
