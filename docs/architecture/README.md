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

Logical guest execution identities are documented in
[A32 logical thread execution context](a32-logical-thread-context.md). That
internal seam reuses the existing A32 execution/service-suspension contracts
and deliberately does not introduce a generic scheduler.

Evidence-backed pthread creation/identity/exit semantics are documented in
[A32 pthread logical lifecycle service](a32-pthread-lifecycle.md).

Guest-visible deterministic clock access is documented in
[ARM32 libc clock_gettime compatibility](a32-libc-clock.md).

Each architecture document describes one boundary and should avoid folding later-layer policy into earlier layers.
