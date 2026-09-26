# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `029-a32-android-namespace-access-policy` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-028 are accepted. Feature 028's requester-aware Android platform
provider passed the exact-head dedicated shim integration, Linux A32 smoke, and
both required Android checks at
`bb70bfd32fc669f576f1007b0fdfcf87f7a5408e`.

Feature 029 is now integrated on that validated base. It adds a caller-owned
direct namespace/SONAME accessibility policy and routes the real liblog fixture
through an exact `android-log-consumer -> app -> platform` gate.

## Implemented runtime

Accepted through feature 028:

- bounded ARM/Thumb execution, resumable SVC state, bounded host-service
  dispatch, and exact-SVC registry composition;
- bounded AAPCS32 `__android_log_write` service;
- reproducible ARMv7 `liblog.so` write shim plus exact catalog identity;
- real consumer -> dependency -> JUMP_SLOT -> shim -> service -> return
  execution evidence;
- requester-aware `A32AndroidPlatformProvider` with explicit access policy,
  application-first/platform-fallback composition, and preserved requester
  identity;
- logical guest-memory, ELF32 load/placement/metadata/dependency/linker,
  relocation, RELRO, lifecycle, and symbol/version seams described in current
  specs.

Current unverified feature-029 implementation:

- exact opaque requester identity -> namespace bindings;
- exact direct source-namespace -> platform-namespace links;
- same-platform namespace access without a link;
- mutually exclusive allow-all versus explicit exact SONAME list;
- explicit Failed results for ambiguous/malformed matching configuration;
- no path inference, transitive links, filesystem/APK search, RUNPATH/RPATH,
  LD_LIBRARY_PATH, preload/RTLD behavior, or config parser;
- real ARM32 liblog integration now exercises the namespace gate before
  platform-provider fallback, relocation, shim SVC, and log service.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 028 at
`bb70bfd32fc669f576f1007b0fdfcf87f7a5408e`:

- ARM32 liblog shim integration `108391108320` — PASS.
- Linux A32 smoke `108391108653` — PASS.
- Android x86_64 address-space probe `108391108515` — PASS.
- Android arm64-v8a cross-build `108391108654` — PASS.

Feature 029 exact-head validation: NOT RUN.

## Deferred / partial

Still outside accepted implementation:

- Android search/permitted paths, filesystem/APK search, RUNPATH/RPATH,
  LD_LIBRARY_PATH, preload/RTLD behavior, and automatic platform-provider
  installation;
- `__android_log_print`/`__android_log_vprint` varargs and broader
  libc/JNI/graphics/audio compatibility;
- stable public embedding API/error contract;
- lazy binding, broader relocation families/formats, TLS/IFUNC;
- destructor/unload/dlopen/dlsym lifecycle;
- Android-device end-to-end evidence and general application compatibility.

## Current blockers / external evidence gaps

- Android native tombstone/backtrace coexistence: BLOCKED on an accessible device environment.
- AArch64 16 KiB Android runtime execution: NOT RUN.
- Project license selection: BLOCKED on maintainer choice.
