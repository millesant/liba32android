# ELF32 and dynamic-linking contract

Status: Accepted current project contract
Last reconciled: 2026-09-25

## L32-E001 — ELF32 mapping

The loader accepts validated little-endian ARM ELF32 `ET_EXEC` and explicit-base `ET_DYN`, maps validated `PT_LOAD` segments through `MappedGuestMemory`, copies file bytes, zero-fills BSS, applies final segment permissions, detects conflicts, and rolls back loader-owned mappings on failure.

## L32-E002 — Shared load planning and placement

Pre-mutation ELF validation/layout is shared through `elf32_load_plan`. Automatic `ET_DYN` placement is deterministic, caller-bounded, non-mutating, preserves host-page and `p_align` constraints, and returns a loader-ready guest base.

## L32-E003 — Structural dynamic metadata

`PT_DYNAMIC` discovery and `Elf32_Dyn` parsing remain structural. Dynamic entries preserve raw signed tags/raw values, require bounded valid guest ranges and `DT_NULL` termination, and do not themselves perform dynamic linking.

## L32-E004 — Linker metadata and strings

Validated linker metadata covers STRTAB/STRSZ, SYMTAB/SYMENT, main REL/RELSZ/RELENT, separate AArch32 PLT REL metadata, SONAME, ordered `DT_NEEDED` offsets, DT_SYMBOLIC/DF_SYMBOLIC requester binding, raw `DT_FLAGS_1` with explicit `DF_1_GLOBAL` membership, and paired `DT_INIT_ARRAY/DT_INIT_ARRAYSZ` plus `DT_FINI_ARRAY/DT_FINI_ARRAYSZ` descriptors. Lifecycle-array addresses are rebased once, sizes are integral ELF32 function-pointer widths, and declared guest ranges are readable. Unknown FLAGS_1 bits are preserved. String materialization is explicitly bounded and preserves ordered/repeated dependency names.

## L32-E005 — Dependency acquisition and graph loading

Dependency acquisition is provider-backed and bounded; filesystem/search-path/namespace policy stays outside the generic core. The provider boundary can additionally receive a borrowed opaque requester identity for each ordered DT_NEEDED occurrence; the default requester-aware hook delegates to the original context-free provider method, and recursive loaders supply the exact currently processed graph-object identity without persisting the borrowed view. A caller-owned finite provider chain may compose providers in explicit order: only NotFound falls through, while hard failure or first success terminates lookup; requester identity, request bytes, and the exact resolver image ceiling are forwarded unchanged. The chain owns no provider, treats null entries as failure, and leaves provider-result validation to the resolver. A caller-owned dependency catalog may expose exact byte-name entries backed by borrowed request-name/identity/image storage: no match returns NotFound, duplicate exact matches or malformed/oversized selected entries fail, and a unique valid match is copied into an owned provider result. Catalog lookup performs no normalization or requester interpretation and composes directly through the provider chain. Provider result identity remains the opaque object key. Recursive one-shot graph loading is transactional, preserves ordered/repeated edges, reuses cycles/aliases, automatically places dependency `ET_DYN` images, and rolls back graph-owned mappings on aggregate failure. Feature 016 additionally provides a caller-owned persistent link map: root appends preserve stable accumulated object indexes/mappings across calls, reuse existing equal identity/image pairs, reject cross-load identity/image mismatches, and roll back only state introduced by a failed append. Its stable global-scope list is deduplicated and ordered by accumulated object discovery, with membership from caller-designated global roots plus validated `DF_1_GLOBAL` objects; the span is directly consumable by feature-015 reference lookup.

## L32-E006 — Symbol resolution

Dynamic-symbol indexing supports bounded SysV/GNU hash processing and exact byte-name lookup. Plain graph resolution remains a deterministic breadth-first dependency scope. Relocation/reference lookup may additionally consume a caller-provided ordered global-scope object list from the same loaded graph: ordinary requesters search that list before their local breadth-first closure, while requesters carrying DT_SYMBOLIC or DF_SYMBOLIC search themselves first, then the explicit global list, then the remaining local closure. Candidate objects are deduplicated and share one caller-selected scope ceiling. DT_VERSYM plus bounded VERNEED/VERDEF matching applies across that ordered candidate scope: indices 0/1 are unversioned, unversioned lookup skips hidden definitions, explicit versions match provider VERDEF hash/name and otherwise global version index 1. VERNEED target SONAMEs must map to direct dependencies. Feature 016 supplies the persistent accumulated graph/global-scope producer consumed by this lookup layer. Android namespace/search-path/preload/RTLD selection policy, TLS, IFUNC, and unsupported special-section semantics remain outside this contract.

## L32-E007 — Main REL relocations

Main `DT_REL` supports bounded planning/resolution/application for `R_ARM_NONE`, `R_ARM_RELATIVE`, `R_ARM_GLOB_DAT`, `R_ARM_ABS32`, and `R_ARM_REL32`. All semantic checks complete before writes; later write failures trigger reverse rollback. `GLOB_DAT` writes `S` and does not use the in-place word as an addend. `REL32` applies `((S + A) | T) - P` modulo 2^32, deriving `T` from the defining Thumb `STT_FUNC` rather than the requester.

## L32-E008 — Eager PLT relocation

The separate PLT REL path accepts eager `R_ARM_JUMP_SLOT`, resolves through the same bounded relocation/reference symbol policy (including caller-provided global scope and DT_SYMBOLIC/DF_SYMBOLIC requester-first ordering), writes `S` directly, and uses the original slot word only for rollback. Lazy binding and `DT_PLTGOT` resolver state remain outside the accepted contract.

## L32-E009 — GNU RELRO

Validated `PT_GNU_RELRO` ranges are exposed as guest-only loader metadata without early sealing. The explicit post-relocation sealing API is caller-bounded, preflights the full page set, deduplicates overlaps, accepts already-read-only pages, changes only RW pages to R, never broadens permissions, and rolls back earlier changes on a later protection failure when possible.

Real post-relocation fixture integration is verified by CI #234 / run `35946857448` at `ff1792457f05bd9dd58740e1b768576b9ad4f1c3`: the real GLOB_DAT targets survive sealing, RELRO becomes read-only, direct writes fail, and non-RELRO permissions remain unchanged.

## L32-E010 — Combined main + eager PLT relocation transaction

The additive combined relocation API fully prepares the supported main `DT_REL` and eager PLT REL tables before mutation, rejects cross-table duplicate write targets, applies main writes before PLT writes, and rolls back failures in reverse across the combined sequence. Direct and rollback failures identify the source table as well as relocation index. The existing main-only and PLT-only APIs retain their independent transaction semantics.

## L32-E011 — Real ARM32 fixture execution

The pinned freestanding NDK ARM32 fixture is loadable through the dependency graph, resolvable through GNU-hash graph lookup, relocatable through the combined main+PLT transaction, sealable through GNU RELRO, and executable through the generic A32 CPU adapter on the Linux validation host. The bounded call harness supplies AAPCS r0/r1 arguments, an 8-byte-aligned mapped guest stack, ARM/Thumb state derived from the defining function symbol, and a same-state return sentinel. This proves integrated host execution only and is not Android-device execution evidence.

## L32-E012 — Symbol versioning

Validated guest-only DT_VERSYM, DT_VERDEF/DT_VERDEFNUM, and DT_VERNEED/DT_VERNEEDNUM descriptors feed a bounded version layer. Relocation references derive their request version from the requester symbol index; provider candidates are filtered with Android/bionic-compatible hidden/default and explicit-version matching without changing the caller-selected candidate ordering. Malformed or oversized version metadata fails explicitly before any relocation write.

The generated feature-014 two-DSO ARM32 fixture records a `LIBC` version requirement and a JUMP_SLOT import; exact-head Linux CI resolves and applies it successfully.

## L32-E013 — Lifecycle array metadata

Validated INIT_ARRAY/FINI_ARRAY descriptors feed a read-only caller-bounded decoder. It returns raw logical 32-bit function values in declaration order, preserves null and all-ones sentinels, rejects non-integral sizes, guest-range overflow, entry ceilings, and read failures explicitly, and never mutates guest memory. It does not filter sentinels or execute guest functions.

## L32-E014 — INIT_ARRAY lifecycle planning

A root-scoped read-only constructor planner traverses reachable dependency edges before the requester in deterministic stored edge order. Transient visiting/complete state suppresses cycles and shared dependencies so each reachable object contributes at most once. Caller ceilings bound unique objects and total raw INIT_ARRAY entries decoded. Null and all-ones values consume the entry budget but are filtered from the call plan; retained calls preserve defining object index, array entry index, and the raw logical 32-bit function value including any Thumb bit. Failure returns no successful partial call list and never mutates guest memory or graph state.

## L32-E015 — Bounded INIT_ARRAY call execution

The generic A32 execution request may carry an optional normalized stop PC. The CPU stops before fetching that address, including when it is the initial PC or is reached by the final permitted instruction, and reports the stop explicitly without changing fixed-budget behavior when absent.

A lifecycle executor consumes planned INIT_ARRAY calls in order. It derives ARM/Thumb state from raw function bit 0, restores a caller-owned 8-byte-aligned guest stack top for each call, sets LR to a caller-selected word-aligned normalized return-stop PC with the matching interworking bit, and applies one finite instruction ceiling per call. Completed constructor guest-memory side effects remain visible if a later call fails. Invalid options/function alignment, CPU exceptions, memory faults, and instruction-budget exhaustion fail explicitly with call/object provenance; when the CPU reports both a memory fault and a generic exception for the same step, the lifecycle result preserves the memory-fault classification. The executor owns no guest mapping/protection/unmap behavior and persists no constructor-called state.

## L32-E016 — FINI_ARRAY destructor planning and execution

A root-scoped read-only destructor planner traverses the same reachable object set as constructor planning, records dependency-first postorder once under a caller object ceiling, then emits objects in exact reverse order. Each object's validated FINI_ARRAY is decoded under one total raw-entry ceiling and emitted in reverse declaration order; null and all-ones sentinels consume budget but do not become calls. Invalid roots/edges, limits, and decode failures return no successful partial call list and never mutate guest memory or graph state.

FINI_ARRAY execution reuses the bounded ARM/Thumb lifecycle call seam: caller-owned aligned stack, normalized return-stop PC, finite per-call instruction ceiling, explicit CPU/memory/budget failures, and preserved side effects from completed calls. No stack mapping, rollback, or persistent called-state is owned by the executor.

## L32-E017 — Deferred linker/runtime scope

Still outside the accepted implementation: Android namespace/search-path/pathname accessibility and platform-provider policy; LD_PRELOAD/RTLD selection semantics; protected-reference self-binding; lazy binding; broader ARM relocation families; RELA/RELR/Android packed relocations; TLS/IFUNC; legacy `DT_INIT/DT_FINI`, `DT_PREINIT_ARRAY`, persisted constructor-called state, recursion state across calls, process argv/envp constructor ABI, `dlopen`/`dlsym`/unload; and guest execution of the real ARM32 fixture on Android. Persistent cross-root object lifetime and generic global-group membership are implemented by feature 016.
