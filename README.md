# liba32android

Experimental, game-agnostic AArch32 compatibility runtime for running 32-bit ARM Android native code inside an AArch64 Android process.

The project keeps CPU execution, guest memory, ELF loading, dynamic-linker semantics, relocation, post-relocation hardening, platform diagnostics, and future application compatibility layers separate. Game-specific behavior does not belong in the generic runtime.

## Current capabilities

The current C++20/CMake runtime provides:

- bounded ARM/Thumb execution with exact stop-PC termination, resumable SVC trap state, and game-agnostic bounded host-service dispatch;
- A32 ARM/Thumb execution through an internal Dynarmic adapter;
- an engine-independent `memory::GuestMemory` seam with deterministic and mapped backends;
- logical 32-bit guest virtual addresses with optional high-base 4 GiB fastmem backing and callback fallback;
- validated ARM ELF32 `ET_EXEC` / `ET_DYN` mapping, shared pre-mutation load planning, and bounded automatic `ET_DYN` placement;
- structural `PT_DYNAMIC` parsing plus validated linker metadata, bounded string materialization, caller-bounded raw INIT_ARRAY/FINI_ARRAY decoding, dependency-first INIT_ARRAY planning, reverse-order FINI_ARRAY planning, and bounded ARM/Thumb constructor/destructor execution;
- bounded provider-backed dependency acquisition, transactional recursive dependency-graph loading, and a persistent cross-root link map with DF_1_GLOBAL/global-root scope ordering;
- SysV/GNU dynamic-symbol indexing and deterministic graph-local symbol lookup;
- transactional main `DT_REL` relocation application for the implemented AArch32 relocation set;
- eager PLT `R_ARM_JUMP_SLOT` relocation application;
- explicit post-relocation GNU RELRO sealing with rollback-aware permission handling;
- reproducible ARMv7 Android ELF fixtures and Android address-space/runtime diagnostic tools.

Current accepted runtime and ELF behavior is defined by `.agent/specs/runtime.md` and `.agent/specs/elf32.md`. Historical feature packages under `specs/` are retained for reference and are not current contract authority.

## Repository layout

```text
src/
  cpu/                 CPU abstraction and Dynarmic adapter
  runtime/             game-agnostic execution/service orchestration
  memory/              guest-memory contracts and mapped address space
  elf/
    loading/           load planning, placement, and mapping implementations
    metadata/          structural dynamic/linker metadata implementations
    linking/           dependency, symbol, and relocation implementations
    hardening/         post-relocation hardening implementations
    internal/          private ELF helpers
    *.h                stable internal ELF interfaces

tests/
  cpu/                 CPU regressions
  runtime/             runtime orchestration regressions
  memory/              guest-memory regressions
  elf/
    unit/              synthetic ELF/linker tests
    integration/       generated real ARM32 fixture tests
    fixtures/          fixture source inputs
    support/           ELF test support

tools/
  android/             Android probes and device/emulator validation harnesses
  fixtures/            reproducible ARM32 fixture builders

cmake/
  tests/               domain-specific test registration
docs/
  architecture/        subsystem design and boundaries
  development/         build, validation, and repository-maintenance guides
  research/            research notes and captured evidence
```

See [docs/development/repository-layout.md](docs/development/repository-layout.md) for ownership and dependency rules.

## Build and test

A normal host validation build is:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The shared-library output is exactly `liba32android.so`.

GitHub Actions additionally builds reproducible ARM32 fixtures and validates Android `x86_64` address-space probing plus the Android `arm64-v8a` cross-build. See [docs/development/build-and-test.md](docs/development/build-and-test.md) for fixture options and CI scope.

## Android validation

Android diagnostics live under `tools/android/`. The page-size validation paths are intentionally split:

- `tools/android/run_android_16k_probe_validation.sh` validates the standalone address-space/JIT probe on x86_64 or AArch64 Android;
- `tools/android/run_android_16k_validation.sh` is the stronger AArch64 runtime path and also exercises `liba32android.so` / Dynarmic runtime behavior.

See [docs/diagnostics.md](docs/diagnostics.md) for crash-marker, fastmem-fallback, emulator, and device procedures.

## Architecture documentation

Start at [docs/README.md](docs/README.md). The ELF pipeline is documented as distinct layers: load planning/mapping, structural dynamic metadata, linker metadata/strings, dependency graph loading, symbol resolution, relocation, and RELRO hardening.

The generic runtime deliberately does **not** yet claim implementation of Android namespace/search-path/platform-provider policy, preload/RTLD semantics, full Android interposition behavior, lazy PLT binding, broad ARM relocation coverage, TLS, libc/JNI/graphics/audio compatibility layers, or general game compatibility.

## Project state and contribution workflow

- `AGENTS.md` contains repository-specific engineering invariants.
- `.agent/project.toml` identifies the project.
- `.agent/specs/` contains accepted current contracts.
- `.agent/STATE.md` records observed implementation/validation state.
- `.agent/NEXT.md` records dependency-ordered next work.
- `.agent/changes/` records bounded substantial changes and evidence.

Generic engineering-control rules are maintained externally in the private `Millesant/.gpt` control plane and are intentionally not vendored into this repository.
