# ELF32 dependency loading

Status: persistent loading complete through feature 020 requester context; exact-head implementation CI PASSed

## Boundary

`elf32_dependency_loader` is the first loaded-object graph layer above the completed single-object and acquisition primitives:

```text
root identity + owned ELF32 bytes
              |
              v
elf32_dependency_loader
      |                 |
      |                 +--> graph/resource accounting
      v
load plan
      |
      +--> ET_EXEC root: fixed load
      |
      +--> ET_DYN root/dependency:
             elf32_dynamic_placement
                    |
                    v
             explicit-base load_elf32
                    |
                    v
             dynamic -> metadata -> strings
                    |
                    v
             elf32_dependency_resolver
                    |
                    v
             provider-owned lookup policy
                    |
                    v
             host-owned image + opaque identity
                    |
                    +--> known identity: append edge/reuse
                    |
                    +--> new identity: register/recurse
```

The original graph API owns per-call object identity/lifetime, placement/loading, recursion, aggregate limits, and rollback. Feature 016 adds an additive caller-owned `Elf32LinkMap`: successive roots share one accumulated graph, stable object indexes, and persistent mappings while provider pathname/namespace policy remains external. It does not perform symbol lookup, relocations, constructors, TLS/RELRO, unload, or execution.

Feature 020 leaves that ownership model unchanged but forwards the exact
currently processed object's opaque identity to the provider for every direct
DT_NEEDED acquisition. Root and nested identities are borrowed synchronously;
the resolver/provider stores no view. Context-free providers continue through
the default fallback. This enables later requester-sensitive Android
namespace/search policy without implementing that policy in the graph loader.

## Object and edge model

Object index 0 is the caller-supplied root. Every successful object owns:

- opaque identity bytes;
- source image bytes;
- `Elf32LoadResult`;
- parsed dynamic entries;
- validated linker metadata;
- materialized linker strings;
- ordered dependency edges.

Provider identity is the object key for the duration of one graph-loading call. The root identity participates in the same namespace so a dependency that resolves back to the root forms a cycle edge rather than a second mapping.

Repeated dependency names remain repeated edges. Different names may alias one identity. Equal identities with different acquired bytes fail explicitly as an identity/image mismatch.

Object and edge vectors preserve deterministic discovery and `DT_NEEDED` occurrence order. Identity lookup is indexed separately so deduplication does not require repeatedly scanning the object vector.

## Placement and traversal

A root `ET_EXEC` retains fixed-address semantics. A root `ET_DYN` is automatically placed and then passed to the unchanged explicit-base loader.

Every first-seen dependency must be `ET_DYN`. It is:

1. validated through the shared load plan;
2. placed with `place_elf32_dynamic`;
3. loaded with the exact returned `dynamic_base`;
4. inspected through dynamic -> linker metadata -> linker strings;
5. resolved through the unchanged acquisition-only dependency resolver;
6. traversed depth-first in direct dependency occurrence order.

Known identities in Loading or Loaded state are reused immediately. This terminates cycles while preserving the edge that caused the cycle.

A loaded object with no `PT_DYNAMIC` succeeds with empty downstream dynamic/linker/dependency state.

## Resource accounting

Every call requires caller-selected limits for:

- unique objects including the root;
- recursion depth with root depth 0;
- total dependency occurrences;
- bytes per root/provider image;
- total acquired/owned image bytes;
- linker-string payload bytes;
- automatic placement search window.

The root is checked against image budgets before guest mutation. Every provider occurrence consumes occurrence and acquired-byte budget even if its identity later deduplicates to an existing object, because the resolver call and image acquisition still occurred.

The graph keeps wide checked accounting and never turns an occurrence duplicate into a free provider lookup.

## Transaction and rollback

Successful mappings are recorded in load order. If any later acquisition, identity check, placement, load, parse, metadata, string, or resource step fails, the graph loader unmaps successful graph-owned mappings in reverse order.

Preexisting guest mappings are never part of the rollback set. A failure returns no partial successful graph. If an expected graph-owned unmap fails, the public error becomes `RollbackFailed` while `primary_error` preserves the original cause.

For persistent-link-map append, objects/mappings that existed before the call are also outside the rollback set. A failed append unmaps successful mappings created by that append, truncates newly added object records, and leaves earlier root records/object indexes intact. Existing identities are reused without remapping; a same identity with different bytes fails explicitly.

## Persistent roots and global scope

Each persistent root records explicit caller policy as local or global. After a
successful append, global membership is extended in stable accumulated object
order:

- a caller-designated global root is eligible at its object index;
- every newly loaded object whose validated `DT_FLAGS_1` carries
  `DF_1_GLOBAL` is eligible;
- duplicates are suppressed; promotion of an older existing root inserts it at
  its original object-discovery position rather than at promotion time;
- local roots do not enter the global list unless their own metadata carries
  `DF_1_GLOBAL`.

The resulting `Elf32LinkMap::global_scope()` span contains ordinary object
indexes from the accumulated graph and can be passed directly to feature-015
relocation/reference lookup. This feature still does not infer Android
namespace accessibility, LD_PRELOAD, or RTLD policy.

The underlying single-image loader remains responsible for rolling back its own partially failed object load before the graph layer records that object as successful.

## Reclamation planning

Post-roadmap reclamation planning keeps the feature-016 stable-index storage
model intact. It does not erase graph objects or unmap memory.

`plan_elf32_link_map_reclamation` treats persistent root records as ownership
anchors and accepts a borrowed set of additional live object anchors for
external owners such as active libdl handles. Reachability follows ordinary
dependency edges transitively with repeated edges, shared dependencies, and
cycles visited once. Global-scope membership is symbol-visibility state only
and does not keep an otherwise unreachable object alive.

The planner returns reachable indexes in stable ascending order and unreachable
indexes in deterministic reverse-postorder teardown order. Outside cycles this
places requesters before their unreachable dependencies; cycle members remain
deterministic but cannot satisfy a strict requester-first ordering for every
edge simultaneously.

The operation is read-only and bounded by the caller's accumulated-object
ceiling. It validates the graph edges, persistent root records, and additional
live anchors that it consumes before returning a successful plan. Destructor
execution, root/global removal, stable-slot tombstoning/reuse, and guest
unmapping remain later transactions.

## Failure surface

The graph layer distinguishes root/options/image limits, object/depth/occurrence limits, nested dependency-resolution errors, identity/image mismatch, malformed or non-dynamic dependencies, placement/load failures, dynamic/metadata/string failures, and rollback failure.

Nested stage-specific error enums remain attached to the result, along with failing identity and requested dependency name when applicable. Guest addresses remain logical 32-bit values; no host pointer is exposed as a guest address.

## Validation

Focused synthetic coverage exercises dependency-free roots, direct dependencies, aliases/repeats, transitive traversal, cycles, shared dependencies, identity/image mismatch, non-dynamic dependency rejection, limits, late failures, rollback, source ownership, and preservation of unrelated mappings. Feature 016 extends that suite with multi-root identity reuse, local-to-global root promotion, DF_1_GLOBAL ordering, failed-append preservation of prior roots/global scope/mappings, accumulated max-object enforcement, full persistent-state preflight (identity uniqueness, dependency-edge targets, root records, exact global-membership consistency, and strictly increasing discovery order), and direct feature-015 global-scope consumption. Feature 020 adds direct root/nested requester-context assertions plus persistent cross-root requester identity coverage.

The pinned NDK ARMv7 fixture is also loaded through the graph API. Its integration requires:

- automatic `ET_DYN` placement;
- `0x4000` load-bias alignment;
- preserved SONAME `liba32android_loader_fixture.so`;
- zero dependency edges;
- zero provider calls.

Pre-convergence head `78665e000a67b559c694aef5b1e22f0742f360c0` PASSed GitHub Actions run `35708717172` (#198): Linux A32 smoke passed 39/39 CTest including `elf32_dependency_loading` and `elf32_real_dependency_loading`; Android x86_64 address-space probe and Android arm64-v8a cross-build also passed.

Final head `1ac47ef59f3570989d6fc07cd187c129cbe76588` PASSed GitHub Actions CI #199 / run `35708717172` with 39/39 CTest and both Android jobs. PR #32 was then squash-merged to `bleeding` as `17c2aa78535adbd2084c9396f525750e10c0eff8`, preserving the validated source tree.

Feature 016 result revision `0c374ff84990ee3d64c06a1037f846d90054e5e4` PASSed GitHub Actions CI run `36201652255` (#299). The Linux suite includes the persistent link-map/global-scope regressions, while Android x86_64 probe and Android arm64-v8a cross-build lanes also completed successfully.

## Deliberate layer-local limits

These are dependency-loader ownership boundaries, not a repository-wide list
of absent features.

This layer does not define:

- Android/bionic search paths, namespace accessibility/linking, RUNPATH/RPATH, or APK/package policy beyond forwarding requester identity to external providers;
- symbol lookup/interposition/versioning or hash-table semantics (implemented downstream for the accepted bounded scope);
- ARM relocation or PLT/JMPREL application (implemented downstream for the accepted REL/JUMP_SLOT subset); packed relocation formats remain deferred;
- RELRO or lifecycle execution (GNU RELRO and bounded INIT_ARRAY execution are downstream); TLS, destructor/unload lifecycle, `dlopen`, and `dlsym` remain deferred;
- concurrent graph mutation / atomic address reservation;
- guest execution itself; host fixture execution and the generic runtime/service layers consume loaded graphs downstream.


## Feature 020 validation

Feature 020 result revision `2509dce17e8d1b993325ed810d37a29e1b46df45`
PASSed Linux A32 smoke and both required Android lanes. Recursive one-shot and
persistent loading tests verify that root and nested graph-object identities are
forwarded unchanged as synchronous requester context while provider-result
identity, rollback, and resource accounting remain unchanged.
