# Stable public C embedding API

Status: feature 049 accepted; exact-head validation PASSed

## Goal

Turn the accepted engine-independent mapped-memory/A32 execution seams into an
installable host-facing ABI without exposing Dynarmic or private C++ types.

The public header is:

`include/liba32android/liba32android.h`

## ABI version and runtime lifetime

`LIBA32ANDROID_API_VERSION` starts at 1 and is returned by
`liba32android_api_version()`.

`liba32android_runtime` is opaque. Creation reserves the same logical 32-bit
guest address space used by `MappedGuestMemory`; destruction releases it.

No public function treats a logical guest address as a host pointer.

## Public memory surface

Version 1 exposes page-size query, map, protect, unmap, read, and write.
Addresses are `uint32_t`; lengths are host `size_t`. Permission bits are
READ/WRITE/EXECUTE and preserve the internal rule that W/X implies R.
Malformed page ranges and logical-address overflow are classified separately
from mapped-memory operation failure.

## Public execution surface

The size-tagged request selects ARM or Thumb, logical entry PC, r0-r15, a
finite non-zero instruction budget, optional stop PC, and optional initial CPSR.

The size-tagged result returns r0-r15, CPSR, executed instruction count, flags
for stop-PC/SVC/fastmem/memory-fault/ordinary-exception, and the exact SVC
immediate when trapped.

SVC is a successful trap outcome so the embedding receives the post-SVC state
and can decide how to continue.

## Error ABI

Every fallible call accepts an optional caller-owned
`liba32android_error_buffer`. Failures use the stable
`A32ERR|component=...|code=...` prefix and append logical PC/address where
known. Required length excludes NUL; supplied storage is NUL-terminated when
capacity is non-zero. Successful calls clear the error buffer.

## Validation

The exact-head public API workflow at
`827fce9fbb55f6106bea5273345ccbb4af94e253` passed. It:

1. runs the pure-C in-tree regression;
2. stages a CMake install;
3. verifies the installed header, shared library, and exported C symbols;
4. compiles the same caller externally using only the installed surface;
5. executes that external consumer successfully.

## Deliberate limits

Version 1 does not freeze the internal ELF loader/link map, dependency-provider,
compatibility service registry, Android namespace/search source, pthread
scheduler handoff, guest heap, libdl/libm service objects, or device integration
as public ABI. Those remain private composition pieces.
