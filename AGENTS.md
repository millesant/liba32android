# Repository Engineering Instructions

This file contains durable project-specific engineering expectations only. Generic AI workflow, task selection, continuation, issue lifecycle, and handoff behavior are intentionally outside this repository.

## Product and architecture invariants

- The runtime remains game-agnostic. Application-specific behavior belongs under `profiles/` and must not leak into the generic core.
- AArch32 guest addresses are logical 32-bit values. CPU, memory, ELF, ABI, compatibility, and runtime interfaces must not expose host pointers as guest pointers.
- Dynarmic stays behind `src/cpu/`; higher layers depend on engine-independent contracts.
- `memory::GuestMemory` remains the generic memory seam. Fastmem is optional acceleration; callback-backed access remains the correctness fallback.
- Keep ELF mapping, structural metadata, dynamic-linker semantics, relocation, lifecycle, and post-relocation hardening as separate layers.
- Do not silently broaden guest permissions, resource ceilings, or compatibility semantics to make a fixture pass.
- Do not freeze private C++ ELF/compatibility objects into the public C embedding ABI without an explicit compatibility decision.

## Toolchain and build constraints

- The project is C++20 and uses CMake 3.24 or newer; Ninja is the CI generator.
- The runtime shared-library identity is `liba32android.so`.
- Android cross-build validation targets `arm64-v8a`; host validation and Android device/runtime claims remain distinct evidence.
- Pinned third-party dependencies and reproducible fixture toolchains must remain auditable. Do not upgrade them as an unrelated side effect.

## Compatibility and evidence rules

- Prefer the smallest generic compatibility contract justified by real AArch32/Android evidence.
- Untrusted guest strings, arrays, metadata, graphs, and service state must remain caller-bounded with deterministic failure behavior.
- Cross-build, emulator, fixture, or device evidence proves only the stated revision/environment; do not generalize beyond it.
- Real third-party binaries may be used as evidence inputs, but do not commit material that the project is not authorized to redistribute.

## Durable engineering truth

- Accepted current contracts live under `docs/contracts/`.
- Subsystem architecture and accepted engineering decisions live under `docs/architecture/`.
- Build, test, and repository guidance live under `docs/development/`.
- Research and environment-specific evidence live under `docs/research/`.
- Update durable documentation when a change alters an accepted contract, architecture boundary, compatibility claim, or reproducible validation requirement.

## Validation and integration

- Match the surrounding C/C++ style and keep ownership, bounds, integer conversions, ABI layout, and error behavior explicit.
- Add or update focused regression coverage when behavior changes.
- Run the narrowest sufficient validation and do not report a check as passing unless it actually ran against the relevant revision.
- CTest names and executable target identities are CI/evidence compatibility surfaces; do not casually rename them.
- `main` is the integration branch. Preserve unrelated work, avoid force-push convenience, and keep changes focused.
