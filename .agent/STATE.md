# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-resident-dlclose-lifecycle-transaction` — IMPLEMENTED, corrected exact-head validation PENDING.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap prerequisites now include service-aware ELF FINI and
linked `__cxa_finalize`.

## Active post-roadmap follow-up

Resident libdl can optionally delegate `dlclose` to a bounded exact-object
lifecycle transaction.

Non-final references decrement only. The final reference runs reverse
FINI_ARRAY through service-aware execution, requires all registrations for the
bound opaque DSO word to be Complete, runs DT_FINI, marks persistent destructor
state Complete, and only then releases the synthetic handle.

Guest teardown failure or incomplete registered finalization latches Failed and
preserves the final handle. Dependency mappings remain resident.

Focused and real ARM32 regressions are integrated.

Exact-head validation at `b6168ee8d98c8583b6cd325efd51f42da382e921`: FAILED only in ARM32 libdl integration; the other eight required checks passed.

The real linked `dlclose` reached the lifecycle transaction but the transaction
started nested FINI execution at the outer configured stack top while the
guest `fixture_dlclose` caller frame was still active. This is the same nested
guest-frame hazard previously fixed for synchronous `__cxa_finalize`.

The corrected transaction accepts an optional trapped live guest r13. The
libdl SVC path passes `regs[13]`, and both FINI_ARRAY and DT_FINI execute below
that active caller frame. A focused regression writes the effective SP from
guest FINI code and verifies the live-stack override.

Corrected exact-head validation at `124deba7bfff005696058d9f67d41e779e32427e`: FAILED only in ARM32 libdl integration.

The diagnostic run reached the exact provider FINI transaction and reported
`fini_array_execution_failed`, with r0 == -1, marker still zero, and the
provider lifecycle state latched Failed.

Root cause was in the integration harness rather than the dlclose transaction:
`apply_elf32_combined_relocations(..., object_index, ...)` relocates one graph
object only, but the harness had relocated object 0 (the consumer) and then
executed the provider's FINI_ARRAY and global-marker code without applying the
provider's own relocations.

The harness now relocates every loaded graph object before guest execution.
Next exact-head validation target: the current `bleeding` head containing the graph-wide relocation correction.

## Deferred / partial

Recursive dependency reference ownership, persistent-link-map removal, safe
mapping reclamation, RTLD_NODELETE/global-group policy, dynamic missing-object
libdl acquisition, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI,
broader pthread/TLS, concrete APK/ZIP byte acquisition, higher-level public
ELF/platform orchestration, JNI/graphics/audio surfaces, and real Android
device execution remain separate.
