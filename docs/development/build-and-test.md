# Build and test

## Host build

Requirements used by CI include CMake 3.24+, Ninja, a C++20 compiler, Boost headers, and binutils.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The runtime target must produce exactly `build/liba32android.so`.

## Real ARM32 fixture tests

The Linux CI job builds ARMv7 Android fixture DSOs with pinned NDK
`27.3.13750724`. Existing loader/JUMP_SLOT/versioning fixtures plus the
feature-027 Android-log pair and feature-032 partial-libc pair are passed to CMake through:

- `LIBA32ANDROID_ARM32_FIXTURE_PATH`
- `LIBA32ANDROID_ARM32_JUMP_SLOT_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_JUMP_SLOT_PROVIDER_PATH`
- `LIBA32ANDROID_ARM32_VERSIONED_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_VERSIONED_PROVIDER_PATH`
- `LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH`
- `LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH`\n- `LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH`\n- `LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH`

ELF-only fixture source lives in `tests/elf/fixtures/`; compatibility fixture
source lives in `tests/compat/fixtures/`; builders live in
`tools/fixtures/`.

## Test organization

CTest registration is split across:

- `cmake/tests/LibA32AndroidCpuTests.cmake`
- `cmake/tests/LibA32AndroidRuntimeTests.cmake`
- `cmake/tests/LibA32AndroidCompatTests.cmake`
- `cmake/tests/LibA32AndroidMemoryTests.cmake`
- `cmake/tests/LibA32AndroidElfTests.cmake`

Keep existing executable target names and CTest names stable unless a deliberate test-contract change requires otherwise.

## Android cross-builds

CI validates two Android configurations:

1. x86_64 standalone address-space probe;
2. arm64-v8a shared runtime plus address-space probe and runtime smoke tool.

The Android build applies 16 KiB ELF maximum/common page-size linker options to project-owned final ELF targets.

A cross-build proves build/link properties for that revision. It does not prove device runtime behavior. Device/emulator runtime claims require the harnesses documented in `docs/diagnostics.md`.
