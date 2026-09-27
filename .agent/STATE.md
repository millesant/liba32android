# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-link-map-reclamation-planning` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF FINI, linked
`__cxa_finalize`, and bounded resident last-reference `dlclose` lifecycle
execution.

The resident dlclose lifecycle follow-up is DONE. Its validated result revision
is `efa2ce78e7d77ddf9c92ee29cdbe5a1f03fc6dd2`.

## Active post-roadmap follow-up

Persistent link-map reclamation planning is implemented as a read-only ownership
and reachability seam.

Every persistent link-map root is an ownership anchor. Callers may additionally
supply borrowed live object anchors representing external owners such as active
libdl handles. Reachability follows dependency edges transitively with shared
dependencies, repeated edges, and cycles visited once.

Global-scope membership is explicitly visibility only and does not retain an
otherwise unreachable object.

The planner validates its accumulated-object ceiling, object identities,
dependency edges, persistent root records, and live anchors before success.
Reachable objects are reported in stable index order. Unreachable objects are
reported in deterministic reverse-postorder: acyclic requesters precede their
unreachable dependencies, while cycle members are deterministic and once-only.

The planner never mutates roots, global scope, graph objects, lifecycle state,
handles, mappings, or protections. Actual destructor execution, root/global
removal, stable-slot tombstoning/reuse, and guest unmapping remain separate.

Focused regressions are integrated in
`tests/elf/unit/elf32_dependency_loader.cpp`.

Exact-head validation: NOT RUN.

## Deferred / partial

Recursive dependency reference ownership, persistent-link-map removal, safe
mapping reclamation, RTLD_NODELETE/global-group policy, dynamic missing-object
libdl acquisition, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI,
broader pthread/TLS, concrete APK/ZIP byte acquisition, higher-level public
ELF/platform orchestration, JNI/graphics/audio surfaces, and real Android
device execution remain separate.
