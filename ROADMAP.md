# Roadmap

liba32android is being built bottom-up: execution and ELF/linker correctness
first, then bounded Android native compatibility surfaces, then increasingly
realistic application bootstrap.

This roadmap describes durable direction, not release promises, dates, or a
mirror of the active GitHub issue queue.

## Current foundation

Implemented and regression-tested foundations include:

- A32 ARM/Thumb execution behind an engine-independent CPU adapter;
- logical 32-bit guest memory plus a versioned public C embedding API;
- ARM ELF32 mapping, placement, metadata, dependency loading, symbol lookup,
  versioning, relocation, lifecycle execution, and GNU RELRO hardening;
- bounded host-service dispatch and logical guest-thread execution identity;
- Android namespace/platform-provider policy and requester-aware filesystem/APK
  native-library discovery;
- evidence-backed partial libc, liblog, libdl, libm, pthread, signal,
  scheduler/priority, clock, and C++ finalization compatibility;
- a partial ARM32 `liblog.so` covering `__android_log_write`,
  `__android_log_print`, and `__android_log_vprint` through one bounded sink
  boundary;
- bounded JNI JavaVM/JNIEnv bootstrap, native registration/invocation,
  references, strings/arrays/fields/exceptions, and per-logical-thread
  attachment/local-reference/pending-exception state;
- reproducible ARM32 fixtures plus Android `arm64-v8a` cross-build and runtime
  probe coverage.

Accepted current engineering contracts live under `docs/contracts/`; subsystem
boundaries live under `docs/architecture/`.

## Near-term direction

Compatibility work remains evidence-driven. The next layers are selected from
actual unresolved imports and runtime needs in supplied ARM32 Android binaries,
not from API-table adjacency or a goal of cloning all of Bionic/ART.

The main remaining native-runtime themes are:

- complete only the evidenced gaps in existing libc/libm compatibility;
- add bounded descriptor/file and stdio behavior behind embedding-owned access
  policy rather than unrestricted host passthrough;
- add bounded socket/network behavior behind explicit embedding-owned policy;
- introduce generic native-window/framework boundaries before graphics APIs;
- add EGL/GLES support only after the window/object lifetime boundary is
  explicit and device verification can distinguish mocked state from real GPU
  behavior;
- continue JNI coverage only where supplied binaries justify additional slots
  or object-model semantics.

GitHub Issues are the authoritative source for active work, acceptance criteria,
and validation handoffs. This document intentionally does not duplicate issue
status or sequencing.

## Later layers

Graphics, audio, input, Android framework classes, and application-specific
integration remain later concerns. They should be introduced only when generic
runtime boundaries are strong enough to keep application behavior out of the
core.

Application-specific quirks belong in profiles or embedding code, not in the
game-agnostic runtime.

## Release-readiness gates

The project has an Apache-2.0 license, documented security reporting, and a
versioned public C ABI. Before calling liba32android a generally consumable
release, it should still have:

- a documented supported platform/toolchain matrix;
- a stable release/versioning policy for the public C ABI;
- reproducible release artifacts;
- representative real-device Android validation;
- clearly documented compatibility limits and unsupported behavior.

See open GitHub Issues for active bounded engineering work and
`docs/architecture/` for subsystem detail.
