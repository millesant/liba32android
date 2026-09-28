# Repository layout

The repository is organized by ownership and runtime layer rather than by feature chronology.

## Runtime source

- `src/cpu/`: engine-independent A32 CPU contract plus Dynarmic adapter. Dynarmic types must not escape this directory.
- `src/runtime/`: game-agnostic orchestration above CPU/memory, including bounded host-service dispatch and exact-SVC registry composition; platform API policy does not belong here.
- `src/compat/`: platform compatibility adapters above the generic runtime seams. Platform-specific ABI/service behavior belongs here rather than in CPU/runtime internals.
- `src/memory/`: logical guest-address and mapped-memory contracts.
- `src/elf/`: ELF interface headers.
  - `loading/`: validation/layout planning, automatic placement, and mapping implementations.
  - `metadata/`: structural dynamic-array parsing plus linker metadata/string implementations.
  - `linking/`: dependency acquisition/loading, symbol lookup, and relocation implementations.
  - `hardening/`: post-relocation permission hardening.
  - `internal/`: private helpers that are not layer contracts.

Headers intentionally remain at `src/elf/` while implementation files are grouped by layer. That keeps include names stable while making implementation ownership visible in the tree.

## Tests

- `tests/cpu/`: CPU execution/fastmem regressions.
- `tests/runtime/`: game-agnostic CPU/memory orchestration regressions.
- `tests/compat/`: platform compatibility ABI/service regressions.
- `tests/memory/`: guest-memory and guest-VA allocator regressions.
- `tests/elf/unit/`: synthetic deterministic ELF/linker tests.
- `tests/elf/integration/`: tests that consume generated real ARM32 fixtures.
- `tests/elf/fixtures/`: C source used to generate reproducible ARM32 ELF inputs.
- `tests/elf/support/`: shared ELF test helpers.

CTest names and executable target identities are compatibility surfaces for CI and evidence. File moves must not casually rename them.

## Build configuration

Top-level `CMakeLists.txt` defines project-wide options and includes focused modules under `cmake/`. Test registration is split by domain under `cmake/tests/` so CPU, runtime, compatibility, memory, and ELF test ownership can evolve independently without recreating target setup boilerplate.

## Tooling

- `tools/android/`: Android probes and validation harnesses.
- `tools/fixtures/`: reproducible ARM32 fixture builders.

Tool scripts must resolve repository inputs relative to their own location and remain usable from any working directory.

## Documentation and state

- `docs/architecture/`: current subsystem design.
- `docs/development/`: contributor/build/repository guidance.
- `docs/research/`: research notes and captured environment evidence.
- `.agent/specs/`: accepted current project contracts.
- `.agent/changes/`: substantial change identity/tasks/evidence.
- `docs/history/`: compact migration/history notes; retired pre-v7 specs remain in Git history rather than the working tree.
