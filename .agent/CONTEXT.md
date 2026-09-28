# Project Context

## Mission

Build a reusable, game-agnostic AArch32 compatibility runtime for AArch64 Android. The intended stack is ARM32 Android ELF -> ELF32 loader -> dynamic-linker semantics -> future ABI/compatibility layers -> A32 execution engine -> AArch64 Android host.

Minecraft PE 0.15.x is a future stress target, not the architecture.

## Non-goals and invariants

- Do not hard-code one application into the generic runtime.
- Guest addresses are logical 32-bit values; guest pointer values are not host pointers.
- Dynarmic stays behind `src/cpu/`.
- `memory::GuestMemory` remains the engine-independent memory seam.
- Fastmem is optional acceleration; callbacks remain the correctness fallback.
- ELF mapping, structural metadata, dynamic-linker semantics, relocation, and post-relocation hardening remain separate layers.
- Do not broaden permissions or compatibility semantics to make a fixture pass.

## Current stack

- Language/build: C++20, CMake 3.24+, Ninja in CI.
- CPU engine: pinned Dynarmic behind the A32 CPU adapter, including exact resumable SVC reporting.
- Runtime services: bounded game-agnostic SVC dispatch above CPU/GuestMemory with caller-owned handlers; no Android API policy in the CPU layer.
- Memory: `LinearGuestMemory` for deterministic correctness tests; `MappedGuestMemory` for logical guest mappings, protection lifecycle, high-base fastmem reservation, and callback fallback.
- ELF loading: shared pre-mutation load planning, explicit mapping, bounded deterministic ET_DYN placement.
- Dynamic-linker scope: structural dynamic entries, validated linker metadata/strings including GNU/SysV version descriptors, bounded dependency acquisition, transactional dependency graph loading, SysV/GNU symbol lookup with bounded version matching, main REL including R_ARM_REL32 plus eager JUMP_SLOT relocation, an opt-in combined main+PLT rollback domain, and explicit GNU RELRO sealing.
- Integrated execution: the pinned freestanding ARM32 fixture executes through the generic CPU adapter on the Linux validation host after dependency loading, combined relocations, BSS initialization, and GNU RELRO sealing; Android-device execution remains a separate evidence gap.
- Android validation: x86_64 standalone address-space probe plus arm64-v8a runtime/probe cross-build; real AArch64 16 KiB runtime execution remains an evidence gap.

## Dependency direction

```text
ELF image
  -> loading
  -> structural metadata
  -> linker metadata + strings
  -> dependency graph
  -> symbol lookup
  -> relocation
  -> RELRO hardening

runtime service dispatch -> GuestMemory + CPU adapter -> Dynarmic
```

## Repository map

- `src/cpu/`: CPU abstraction and Dynarmic adapter.
- `src/runtime/`: game-agnostic execution orchestration such as bounded host-service dispatch.
- `src/memory/`: guest-memory contracts and address-space implementation.
- `src/elf/*.h`: ELF layer contracts.
- `src/elf/loading/`: load planning, placement, and mapping implementations.
- `src/elf/metadata/`: dynamic/linker metadata and string implementations.
- `src/elf/linking/`: dependency, symbol, and relocation implementations.
- `src/elf/hardening/`: post-relocation hardening implementations.
- `src/elf/internal/`: private ELF helpers.
- `tests/cpu/`, `tests/runtime/`, `tests/memory/`, `tests/elf/`: subsystem tests; ELF synthetic and real-fixture tests are separated.
- `tools/android/`: Android diagnostics and runtime-validation harnesses.
- `tools/fixtures/`: reproducible ARM32 fixture builders.
- `cmake/tests/`: domain-specific test registration.
- `docs/architecture/`: current subsystem design.
- `docs/development/`: build/test/layout guidance.
- `docs/research/`: research and environment-specific evidence.
- `.agent/specs/`: accepted current contracts.
- `.agent/changes/`: substantial change records/evidence.
- `docs/history/pre-v7-specs.md`: retrieval note for the retired pre-v7 root specification tree.

## Canonical state

- Repository integration branch: `bleeding`.
- Project identity: `.agent/project.toml`.
- Accepted current contracts: `.agent/specs/`.
- Observed current state: `.agent/STATE.md`.
- Dependency-ordered next work: `.agent/NEXT.md`.
- Durable project decisions: `.agent/DECISIONS.md`.
- Repository-specific agent overlay: `AGENTS.md`.
- Generic workflow/control rules: private external `Millesant/.gpt` control plane.

For substantial work, use a stable `.agent/changes/<change-id>/` identity. Retired pre-v7 feature packages remain available in Git history, but they are not current contract authority.
