# Stable embedding API gap and boundary evidence — 2026-09-27

## Repository gap before feature 049

The accepted repository audit and diagnostics explicitly record that the
internal runtime/service orchestration exists but no stable public
embedding/error API exists.

At the feature-048 prepared revision:

- `liba32android` publishes only a private `src/` include directory;
- there is no `include/liba32android/` public header tree;
- there is no CMake install rule for the runtime library/header surface;
- the engine-independent CPU and mapped-memory seams remain private C++ APIs;
- diagnostics already define the intended stable `A32ERR|...` text shape but
  mark public delivery as not implemented.

This is a concrete packaging/API gap rather than a missing CPU engine.

## Selected feature-049 boundary

Promote only the lowest stable embedding primitives that already have accepted
internal contracts:

- create/destroy an opaque runtime owning mapped guest memory;
- query host page size;
- logical guest map/protect/unmap/read/write;
- bounded ARM/Thumb execution with optional stop PC and initial CPSR;
- exact SVC trap publication;
- structured stable error text.

The public boundary is a C ABI. It contains only fixed-width integers, size
values, raw caller buffers, size-tagged request/result structs, and an opaque
runtime pointer. No Dynarmic or C++ standard-library type crosses it.

## Why ELF/platform APIs remain private

The current ELF/linker/compatibility stack has many accepted internal contracts,
but features 046-048 are still expanding resident libdl, libm, and Android
application-library search behavior. Promoting those orchestration types in the
same ABI change would freeze policy and lifetime assumptions prematurely.

The initial public surface therefore makes the execution substrate embeddable
without claiming a complete process-loader API. Later API versions can promote
higher-level loader/platform operations without breaking version-1 callers.
