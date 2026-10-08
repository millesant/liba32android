# Architecture index

The runtime is intentionally layered. Dependencies should flow toward lower-level contracts rather than sideways into implementation details.

```text
ARM ELF32 image
  -> loading
  -> structural metadata
  -> linker metadata + strings
  -> dependency graph
  -> symbol lookup
  -> relocation
  -> RELRO hardening

public C embedding API
  -> mapped GuestMemory + CPU adapter
  -> Dynarmic (internal implementation detail)

runtime host-service dispatch
  -> GuestMemory + CPU adapter
  -> Dynarmic (internal implementation detail)

Android compatibility providers/services
  -> runtime + ELF/linker contracts
  -> caller-owned platform/search policy and byte sources
```

Game-agnostic execution orchestration lives under `src/runtime/`. The stable
host-facing ABI lives under `include/liba32android/` with its adapter in
`src/public/`; it promotes only mature memory/execution contracts and does not
make private ELF/compatibility C++ objects public. ELF implementation files are
grouped under `src/elf/loading/`, `metadata/`, `linking/`, and
`hardening/`. ELF interface headers remain at `src/elf/` so internal layer
contracts are easy to discover and include paths stay stable within the
repository.

## Compatibility boundaries

Current compatibility architecture is split by ownership and ABI boundary:

- [logical guest-thread context](a32-logical-thread-context.md),
  [pthread lifecycle](a32-pthread-lifecycle.md), and
  [pthread synchronization](a32-pthread-sync.md);
- [signal](a32-signal-compat.md) and
  [scheduler/priority](a32-scheduler-compat.md) compatibility;
- bounded libc [memory/string](a32-libc-memory-string-service.md),
  [integer](a32-libc-integer-service.md), [heap](a32-libc-heap.md), and
  [clock_gettime](a32-libc-clock.md) services plus the
  [partial ARM32 libc shim](a32-libc-memory-string-shim.md);
- Android [liblog](android-log-write-shim.md),
  [platform catalog](android-platform-catalog-provider.md), and
  [namespace access](android-namespace-access-policy.md) boundaries;
- [libdl](a32-libdl.md), [libm](a32-libm.md),
  [resident dlclose lifecycle](a32-libdl-close-transaction.md),
  [C++ finalization](a32-cxa-finalize.md), and [JNI](a32-jni.md).

Each architecture document describes one boundary and should avoid folding
later-layer policy into earlier layers. Accepted current behavioral contracts
live under `../contracts/`; issue status and validation handoffs stay in GitHub
Issues rather than architecture prose.
