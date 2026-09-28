# Build and test

## Host build

Requirements used by CI include CMake 3.24+, Ninja, a C++20 compiler, Boost
headers, binutils, and zlib development headers.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The runtime target must produce exactly `build/liba32android.so`.

To validate the installed public surface:

```sh
cmake --install build --prefix "$PWD/build-install"
test -f build-install/include/liba32android/liba32android.h
test -f build-install/lib/liba32android.so
```

See [public-api-quickstart.md](public-api-quickstart.md) for the external C
consumer flow.

## Real ARM32 fixture tests

CI generates ARMv7 Android fixture DSOs with pinned Android NDK
`27.3.13750724`. Fixture inputs are supplied to CMake through optional cache
paths, including:

- `LIBA32ANDROID_ARM32_FIXTURE_PATH`
- `LIBA32ANDROID_ARM32_JUMP_SLOT_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_JUMP_SLOT_PROVIDER_PATH`
- `LIBA32ANDROID_ARM32_VERSIONED_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_VERSIONED_PROVIDER_PATH`
- `LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH`
- `LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH`
- `LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH`
- `LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH`
- `LIBA32ANDROID_ARM32_LIBM_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_LIBM_SHIM_PATH`
- `LIBA32ANDROID_ARM32_APP_SEARCH_ROOT_PATH`
- `LIBA32ANDROID_ARM32_APP_SEARCH_APK_PATH`
- `LIBA32ANDROID_ARM32_JNI_ONLOAD_FIXTURE_PATH`

ELF-only fixture sources live in `tests/elf/fixtures/`; compatibility fixture
sources live in `tests/compat/fixtures/`; builders live in `tools/fixtures/`.

Fixture builders should be deterministic. CI commonly builds a fixture twice
and compares the outputs before using it as validation evidence.

## Test organization

CTest registration is split across:

- `cmake/tests/LibA32AndroidCpuTests.cmake`
- `cmake/tests/LibA32AndroidRuntimeTests.cmake`
- `cmake/tests/LibA32AndroidCompatTests.cmake`
- `cmake/tests/LibA32AndroidMemoryTests.cmake`
- `cmake/tests/LibA32AndroidElfTests.cmake`

Keep existing executable target names and CTest names stable unless a deliberate
test-contract change requires otherwise.

For a focused run:

```sh
ctest --test-dir build -R '<pattern>' --output-on-failure
```

Inspection is not test execution. Historical CI success does not validate a
newer local revision.

## Android cross-builds

CI validates two Android configurations:

1. x86_64 standalone address-space probe;
2. arm64-v8a shared runtime plus address-space probe and runtime smoke tool.

The Android build applies 16 KiB ELF maximum/common page-size linker options to
project-owned final ELF targets.

A cross-build proves build/link properties for that revision. It does not prove
device runtime behavior. Device/emulator runtime claims require the harnesses
documented in `docs/diagnostics.md`.

## CI workflows

The main `.github/workflows/ci.yml` covers the host build/test suite, real ARM32
fixtures, Android cross-builds, and probe artifact generation. Focused
compatibility workflows additionally validate the public C API and selected
libc/libdl/libm/JNI/application-search integration seams.

CI artifacts are validation evidence for a specific commit; they are not
release artifacts unless a release process explicitly says so.
