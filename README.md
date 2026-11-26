# liba32android

An AArch32 compatibility runtime that executes 32-bit ARM (ARMv7) Android native code inside a 64-bit Android process. It is an experimental, game-agnostic research runtime — not yet a drop-in Android compatibility layer, and it makes no general APK-compatibility claims.

## What it does

- **A32/Thumb execution** through a private [Dynarmic](https://github.com/azahar-emu/dynarmic) JIT adapter (pinned commit; never exposed above `src/cpu/`), with exact SVC trapping, resumable guest execution, and bounded instruction budgets.
- **Logical 32-bit guest memory** — guest addresses are logical 32-bit values, never host pointers. `memory::GuestMemory` is the single memory seam: `LinearGuestMemory` for deterministic tests, `MappedGuestMemory` for real pages with permissions and optional high-base 4 GiB fastmem acceleration, callback-backed access as the correctness fallback.
- **ARM ELF32 loading and linking** — validated `ET_EXEC`/`ET_DYN` mapping, deterministic dynamic placement, structural dynamic metadata, dependency graphs backed by caller-owned byte providers, symbol lookup with SysV/GNU hash tables and versioning, `R_ARM_*` relocation, PLT `R_ARM_JUMP_SLOT`, init/fini lifecycle planning, and GNU RELRO sealing.
- **Android compatibility services** — bounded, evidence-driven shims dispatched on exact SVC immediates: partial libc (memory/string, integer, heap, clock), pthread lifecycle/synchronization, signal, scheduler, libdl open/close/unload transactions, libm, `__android_log_write`, namespace/platform-library policy, requester-aware library search including APK-embedded DSOs.
- **Bounded JNI layer** — JavaVM/GetEnv/JNI_OnLoad bootstrap, native registration and zero-argument reverse dispatch, class/member registries, reference bookkeeping, strings/arrays/fields/exceptions with per-thread attachment state.

The public surface is a versioned C embedding API (`include/liba32android/liba32android.h`), installed as exactly `liba32android.so`.

## Build

Requires CMake ≥ 3.24, Ninja, a C++20 compiler, Boost headers, binutils, and zlib development headers. Dynarmic and other dependencies are fetched and pinned at configure time.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Cross-builds target Android `arm64-v8a`; all Android targets are linked with 16 KiB page alignment (`-Wl,-z,max-page-size=16384`).

### Tests

Around 160 CTest registrations across CPU, runtime, memory, ELF, and compat subsystems — hand-rolled executables, no test framework. Real ARM32 Android fixtures are generated deterministically with pinned NDK `27.3.13750724` (built twice and compared), supplied via `LIBA32ANDROID_ARM32_*_PATH` cache variables, and builders live in `tools/fixtures/`. Fixture-gated integration tests activate automatically when their fixture path is configured.

## Repository layout

```
include/liba32android/   public C embedding API
src/public/              C API adapter (validation, status/error translation)
src/cpu/                 engine-independent CPU contract + Dynarmic adapter
src/memory/              GuestMemory seam, Linear/Mapped backends, VA allocator
src/runtime/             service dispatch, SVC registry, logical threads
src/elf/                 ELF32 loader/linker (headers flat, impls by layer:
                         loading/, metadata/, linking/, hardening/, internal/)
src/compat/              Android compatibility services (libc, pthread, libdl,
                         libm, log, JNI, namespace policy, library search)
tests/                   per-subsystem tests; elf/unit, elf/integration, fixtures
tools/                   Android probes, fixture builders, benchmark
cmake/                   build modules and per-subsystem test registration
```

Dependencies flow strictly downward: compat → runtime/elf → cpu/memory. The public C API promotes only memory/execution contracts; ELF and compat internals stay C++-internal by design.

## Status

Active research/engineering project. Working today: CPU execution, guest memory, ELF32 load/link/relocate/RELRO, bounded service dispatch, and the partial compatibility surfaces listed above. Not yet implemented: file descriptors/stdio, sockets, native-window/graphics (EGL/GLES), audio. Full platform support matrix and release/versioning policy are pending; see [SECURITY.md](SECURITY.md) for reporting.

## License

Apache-2.0.
