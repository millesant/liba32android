# Design — ARM EABI memory helpers

## Host memmove

Reserve private SVC 0xB2 for memmove. Validate count and both logical guest
ranges under the existing transfer ceiling. Zero count accesses no guest
memory. For non-zero calls, read the complete source into temporary storage
before the destination write, which preserves overlap semantics and source-read
failure atomicity.

## Guest ABI adapters

Export plain memmove as a minimal SVC stub.

The six EABI memcpy/memmove functions have the same register argument shape as
their libc primitives and therefore use direct SVC stubs.

ARM EABI memset uses (r0=dest, r1=count, r2=value), while the existing libc
memset service consumes (r0=dest, r1=value, r2=count). Each memset helper swaps
r1/r2 through caller-saved r3 before SVC.

Each memclr helper moves count from r1 to r2, places zero in r1, and dispatches
the existing memset service. The 4/8 suffixes do not introduce new semantic
alignment checks because bionic delegates them to the same libc operation.

## Integration

The generated consumer imports the full thirty-symbol partial-libc surface and
exposes one wrapper for every new helper. The dedicated integration requires an
eager JUMP_SLOT target for every import and executes every wrapper.

## Exclusion

__aeabi_atexit is lifecycle state, not a memory primitive. It stays deferred
with persistent static-destructor registration.
