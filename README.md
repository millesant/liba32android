# liba32android

**Language:** English | [Português (Brasil)](README.pt-BR.md)

[![CI](https://github.com/millesant/liba32android/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/millesant/liba32android/actions/workflows/ci.yml)
[![Public C embedding API](https://github.com/millesant/liba32android/actions/workflows/public-embedding-api.yml/badge.svg?branch=main)](https://github.com/millesant/liba32android/actions/workflows/public-embedding-api.yml)

**liba32android** is an experimental AArch32 compatibility runtime for executing
32-bit ARM Android native code inside a 64-bit Android process.

The project is building the low-level pieces deliberately: A32 CPU execution,
logical 32-bit guest memory, ARM ELF32 loading/linking, Android compatibility
services, and a bounded JNI layer. It is game-agnostic by design; application
quirks do not belong in the generic runtime.

> **Maturity:** active research/engineering project. The public C embedding API
> is versioned and tested, but the project is not yet a drop-in Android
> compatibility layer and does not claim general APK/game compatibility.

## Why this exists

Modern Android devices are overwhelmingly 64-bit, while a large body of older
Android native software still ships ARMv7/AArch32 code. liba32android explores a
clean compatibility-runtime approach instead of baking one application's
behavior into an emulator or loader fork.

The engineering model is intentionally evidence-driven:

- real ARM32 Android ELF fixtures are generated with a pinned Android NDK;
- loader, linker, relocation, lifecycle, and compatibility behavior is bounded
  and regression-tested;
- Android arm64-v8a builds and 16 KiB ELF/page-size requirements are validated
  in CI;
- real-world ARM32 libraries are used as compatibility evidence without being
  checked into the repository.

## What works today

The current runtime includes:

- a versioned installable **C API v1** for runtime lifetime, guest memory,
  bounded ARM/Thumb execution, exact SVC trapping, and structured diagnostics;
- A32 ARM/Thumb execution through a private Dynarmic adapter;
- logical 32-bit guest virtual addresses with mapped and callback-backed memory
  paths;
- validated ARM ELF32 `ET_EXEC` / `ET_DYN` mapping and bounded dynamic
  placement;
- dynamic metadata, dependency loading, symbol lookup, SysV/GNU hashes,
  symbol versioning, relocation, PLT `R_ARM_JUMP_SLOT`, and GNU RELRO sealing;
- constructor/destructor lifecycle planning and bounded execution;
- Android namespace/platform-library policy and requester-aware library search;
- partial compatibility surfaces for libc, liblog, libdl, libm, pthread-style
  synchronization, `__aeabi_atexit` / `__cxa_finalize`, and related services;
- JNI VM/GetEnv/JNI_OnLoad bootstrap plus bounded FindClass,
  RegisterNatives, member-ID lookup, JavaVM attach/detach state,
  strong/local reference bookkeeping, seeded array-length metadata, and
  zero-Java-argument registered-native reverse dispatch;
- reproducible ARMv7 Android fixtures plus Android address-space/runtime probes.

The normal build registers dozens of host CTest regressions, with additional
fixture/integration workflows in GitHub Actions.

## What is intentionally not claimed

This is not yet:

- a complete Java VM or Android Runtime replacement;
- a complete JNI implementation;
- a complete Bionic/libc/pthread/TLS implementation;
- a complete Android linker/filesystem/APK model;
- a graphics, audio, input, or framework compatibility stack;
- a guarantee that an arbitrary legacy APK or game will run;
- a production-stable ABI for the private C++ ELF/compatibility internals.

The public C API is intentionally much smaller than the internal runtime while
those layers are still evolving.

## Quick start

Host requirements used by CI are CMake 3.24+, Ninja, a C++20 compiler, Boost
headers, binutils, and zlib development headers.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The shared library is produced as:

```text
build/liba32android.so
```

The public header is:

```text
include/liba32android/liba32android.h
```

To stage an install:

```sh
cmake --install build --prefix /tmp/liba32android-install
```

See [Public C API quick start](docs/development/public-api-quickstart.md) for an
external-C-caller example and compile command.

## Architecture at a glance

```text
host embedding API
       |
       v
runtime orchestration + host services
       |
       +----> logical GuestMemory
       |
       +----> A32 CPU adapter ----> Dynarmic
       |
       +----> ARM ELF32 loader/linker
       |         |
       |         +----> dependencies / symbols / relocations / RELRO
       |
       +----> Android compatibility services
                 |
                 +----> libc / liblog / libdl / libm / pthread / JNI
```

The layering rules matter: host pointers never become guest pointers, Dynarmic
does not escape `src/cpu/`, and platform-specific behavior stays above generic
CPU/memory/ELF contracts.

Start with [docs/README.md](docs/README.md) and the
[architecture index](docs/architecture/README.md).

## Repository layout

```text
include/liba32android/   stable public C embedding API
src/public/              public API implementation
src/cpu/                 A32 CPU abstraction and Dynarmic adapter
src/runtime/             generic execution/service orchestration
src/memory/              guest-memory implementations
src/elf/                 ARM ELF32 loader/linker layers
src/compat/              Android compatibility services/adapters

tests/                   unit and integration regressions
tools/fixtures/          reproducible ARM32 fixture builders
tools/android/           Android diagnostic/validation harnesses
docs/                    architecture, development, and research notes
.agent/specs/            accepted current internal project contracts
```

For ownership/dependency rules, see
[docs/development/repository-layout.md](docs/development/repository-layout.md).

## Roadmap

The public roadmap is in [ROADMAP.md](ROADMAP.md). The immediate compatibility
track has completed the first class/native-registration, member-ID,
JavaVM attach/detach, strong/local-reference, seeded GetArrayLength,
GetStaticIntField, and modified-UTF-8 string seams. Current work is adding the
observed four-call jlong array family before broader primitive/object arrays,
field/method, and exception behavior.

## Contributing

Contributions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md), which
covers build/test requirements, scope boundaries, and what makes a useful bug
report or compatibility contribution.

Repository-specific engineering invariants for automated maintainers are in
[AGENTS.md](AGENTS.md). Human contributors do **not** need access to the
maintainer's external automation/control-plane repository.

For security-sensitive reports, see [SECURITY.md](SECURITY.md). Community
expectations are documented in [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Project state

Current accepted technical contracts live under `.agent/specs/`; observed
implementation/validation state is summarized in `.agent/STATE.md`, and
dependency-ordered next work is recorded in `.agent/NEXT.md`. The retired
pre-v7 root specification tree remains available in Git history; see the
[historical specs note](docs/history/pre-v7-specs.md).

## License

liba32android is licensed under the [Apache License 2.0](LICENSE).

Third-party dependencies retain their own licenses. The project does not vendor
the supplied real-world ARM32 evidence binaries used during compatibility
research.
