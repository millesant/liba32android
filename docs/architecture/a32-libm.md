# ARM32 shared libm compatibility

Status: feature 047 implemented; exact-head validation pending

## Scope

Feature 047 implements the seventeen `libm.so` functions shared by the supplied
FMOD ARM32 image and VLC ARMv7 native set:

`acos, asin, atan2, cos, cosf, exp, floor, frexp, ldexp, log, log10, log10f,
pow, powf, sin, sinf, tan`.

The wider VLC-only libm surface remains outside this slice.

## Guest ABI

Private SVC IDs `0xC1` through `0xD1` map one-to-one to the functions above.

The generated consumer and shim use Android ARMv7 softfp. Double arguments are
decoded from core-register pairs and double results are returned in r0/r1.
Float arguments/results use their raw 32-bit register words. Binary doubles use
r0/r1 then r2/r3. `frexp` writes its exponent through the logical guest
pointer in r2; `ldexp` decodes the signed r2 exponent.

All translation uses raw bit copies. No guest floating value is represented by
a host pointer.

## Host numerical engine

`A32LibmService` calls the corresponding host C++ math primitive. Before each
operation it snapshots host errno and floating-point environment and restores
both afterward. This prevents a guest domain/range/exception side effect from
changing embedding-process math state.

Feature 047 intentionally does not synthesize guest errno or guest floating
exception flags. Those policies need a separate thread-local/fenv contract.

## Real ELF path

The pinned-NDK fixture builds:

- generated ARM32 `libm.so` with seventeen direct SVC exports;
- a freestanding ARM32 consumer importing all seventeen symbols.

The integration loads the consumer application-first and the shim through the
requester-aware namespace-gated platform catalog, resolves every symbol from
the shim, applies exactly seventeen eager JUMP_SLOT relocations, and executes
every wrapper through the service registry.

Stable exact-value cases avoid tolerance-policy ambiguity: zero/one identities,
integer powers, floor, and exact binary `frexp/ldexp` values prove argument and
result placement. The frexp case also proves little-endian guest exponent
publication.

## Limits

No complete libm claim, vector/complex API, VLC-only extra symbols,
architecture-specific bionic implementation equivalence, guest errno/fenv
exception publication, or alternate rounding-mode compatibility is introduced.
