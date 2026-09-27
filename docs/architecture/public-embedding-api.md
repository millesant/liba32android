# Stable public C embedding API

Status: feature 049 implemented; exact-head validation pending

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

Version 1 exposes:

- page-size query;
- map;
- protect;
- unmap;
- read;
- write.

Addresses are `uint32_t`. Lengths are host `size_t`. Permission bits are
READ/WRITE/EXECUTE and preserve the internal mapping rule that W/X implies R.
Page operations classify unaligned, zero-length, or wrapping ranges as invalid
arguments before touching mapped state; read/write likewise reject logical
address overflow distinctly from unmapped/permission failures.

## Public execution surface

The caller fills a size-tagged request containing:

- ARM or Thumb;
- logical entry PC;
- r0-r15;
- finite non-zero instruction budget;
- optional stop PC;
- optional exact initial CPSR.

The size-tagged result returns:

- r0-r15;
- CPSR;
- executed instruction count;
- flags for stop-PC, SVC, fastmem, memory fault, and ordinary exception;
- exact SVC immediate when trapped.

SVC is not collapsed into a generic error. It is a successful trap result so a
host can inspect the post-SVC state and make its own continuation/service
decision.

## Error ABI

Every fallible call accepts an optional
`liba32android_error_buffer { data, capacity, required }`.

Failures produce the stable diagnostic prefix:

`A32ERR|component=...|code=...`

and add logical PC/address fields where known. `required` is the complete
message size excluding NUL. Supplied storage is NUL-terminated even if
truncated. Successful calls clear the error buffer.

No exception is intentionally allowed to cross the C ABI.

## C compatibility proof

A pure-C regression includes only the installed-style public header. It:

1. creates the runtime and queries page size;
2. verifies invalid page alignment and wrapping read ranges are classified as
   invalid arguments;
3. maps/writes/protects a logical guest code page;
4. executes ARM `mov r0,#42; bx lr` to a stop PC;
5. executes `svc #0x12` and verifies the exact successful trap;
6. performs an unmapped read and verifies the structured memory error;
7. proves a later successful call clears the error buffer;
8. unmaps and destroys the runtime.

A dedicated host workflow additionally performs a staged CMake install, checks
the installed public header/shared library, compiles the same caller as external
C against only that installed surface, and runs it.

## Packaging

The public include directory is a CMake BUILD_INTERFACE/INSTALL_INTERFACE usage
requirement. CMake installs the shared library and
`include/liba32android` header tree.

## Deliberate limits

Version 1 does not freeze the internal ELF loader/link map, dependency-provider,
compatibility service registry, Android namespace/search source, pthread
scheduler handoff, guest heap, libdl/libm service objects, or device integration
as public ABI. Those remain private composition pieces.
