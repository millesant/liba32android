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

The first registration, observed member-ID, bounded JavaVM attach/detach,
strong/local-reference, seeded GetArrayLength, GetStaticIntField,
modified-UTF-8 string, jlong-array, object-array, instance-long-field,
ThrowNew pending-exception, CallVoidMethodV, raw CallVoidMethod, seeded
byte-array element lease, GetIntField, ExceptionOccurred/ExceptionClear, and
NewObjectV, GetStaticMethodID, raw CallStaticVoidMethod, and raw
CallStaticObjectMethod steps are complete. Direct supplied VLC ARMv7 machine
code now selects the next balanced reference-lifetime seam:

1. NewWeakGlobalRef/DeleteWeakGlobalRef at observed slots 226/227, offsets
   `0x388`/`0x38c`, with weak ownership explicitly separated from strong
   local/global liveness;
2. remaining reference lifetime, including local-frame behavior when balanced
   evidence exists;
3. object construction and broader method invocation / exception APIs;
4. remaining string/primitive-array/field families beyond the currently
   evidence-backed seams;
5. remaining exception operations;
6. remaining object creation and method-call families;
7. remaining instance/static field operations;
8. remaining JavaVM/thread calls where evidence requires them;
9. direct buffers, critical access, and monitors;
10. evidence-driven completion of remaining native-facing JNI slots.

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
