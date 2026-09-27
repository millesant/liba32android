# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none — `post-roadmap-resident-dlclose-lifecycle-transaction` is DONE.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work now includes service-aware ELF FINI, linked
`__cxa_finalize`, and bounded resident last-reference `dlclose` lifecycle
execution.

## Completed post-roadmap follow-up

Resident libdl can optionally delegate `dlclose` to a bounded exact-object
lifecycle transaction.

Non-final references decrement only. The final reference runs reverse
FINI_ARRAY through service-aware execution, requires all registrations for the
bound opaque DSO word to be Complete, runs DT_FINI, marks persistent destructor
state Complete, and only then releases the synthetic handle.

Guest teardown failure or incomplete registered finalization latches Failed and
preserves the final handle. Dependency mappings remain resident.

The first real ARM32 integration exposed nested FINI execution reusing the outer
configured stack top while the guest `fixture_dlclose` caller frame was still
active. The transaction now accepts the trapped live guest r13 and executes
FINI_ARRAY/DT_FINI below that active caller frame.

A later diagnostic run reached provider FINI but failed before writing its
marker. The integration harness had relocated only dependency-graph object 0
even though `apply_elf32_combined_relocations` is explicitly per-object. The
harness now relocates every loaded object before guest execution.

Exact-head validation at
`efa2ce78e7d77ddf9c92ee29cdbe5a1f03fc6dd2`: PASSED. The project operator
confirmed all required CI checks were green after both corrections.

## Next post-roadmap direction

Model recursive dependency ownership/reachability and safe object reclamation.
Only after those invariants are explicit should persistent link-map entries be
removed or guest mappings be unmapped.

## Deferred / partial

Recursive dependency reference ownership, persistent-link-map removal, safe
mapping reclamation, RTLD_NODELETE/global-group policy, dynamic missing-object
libdl acquisition, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI,
broader pthread/TLS, concrete APK/ZIP byte acquisition, higher-level public
ELF/platform orchestration, JNI/graphics/audio surfaces, and real Android
device execution remain separate.
