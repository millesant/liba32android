# Roadmap

liba32android is being built bottom-up: execution and ELF/linker correctness
first, then Android compatibility surfaces, then increasingly realistic native
application bootstrap.

This roadmap describes direction, not release promises or dates.

## Current foundation

Implemented and regression-tested foundations include:

- A32 ARM/Thumb execution and logical guest memory;
- public C embedding API v1;
- ARM ELF32 mapping, placement, metadata, dependency loading, symbol lookup,
  versioning, relocation, lifecycle, and RELRO;
- bounded host-service dispatch;
- Android namespace/platform-library policy;
- partial libc/liblog/libdl/libm/pthread compatibility;
- requester-scoped application native-library search;
- JNI JavaVM/GetEnv/JNI_OnLoad bootstrap plus bounded FindClass, RegisterNatives, evidence-backed member IDs, and no-argument registered-native reverse dispatch.

## Active track: JNI compatibility

The first registration and observed member-ID steps are complete. Direct
supplied-binary evidence has pulled a small JavaVM thread-state seam forward:
AttachCurrentThread/DetachCurrentThread is active now. After that, continue with:

1. local/global/weak reference lifetime;
3. strings and modified UTF-8 APIs;
4. primitive/object arrays;
5. pending exception state and exception operations;
6. object creation and method-call families;
7. instance/static fields;
7. remaining JavaVM/thread calls where evidence requires them;
8. direct buffers, critical access, and monitors;
9. evidence-driven completion of remaining native-facing JNI slots.

The goal is a compatibility layer with explicit limits and deterministic guest
handles, not an accidental full Java VM hidden inside the JNI adapter.

## Parallel compatibility work

As real libraries require it, the project will continue to improve:

- Bionic/libc and pthread/TLS behavior;
- Android dynamic-linker namespace/search/lifetime behavior;
- application/APK native-library discovery and loading;
- destructor/finalization and unload semantics;
- diagnostics and Android device/emulator validation.

## Later layers

Graphics, audio, input, Android framework classes, and application-specific
integration are intentionally later concerns. They should be introduced only
after the generic native runtime boundary is strong enough to keep those layers
separate.

## Release-readiness gates

The project now has an Apache-2.0 license and a documented security-reporting
path. Before calling liba32android a generally consumable release, it should
still have:

- a documented supported platform/toolchain matrix;
- stable release/versioning policy for the public C ABI;
- reproducible release artifacts;
- representative real-device Android validation;
- clearly documented compatibility limits.

See `.agent/NEXT.md` for the maintainers' exact dependency-ordered engineering
queue and `docs/architecture/` for subsystem detail.
