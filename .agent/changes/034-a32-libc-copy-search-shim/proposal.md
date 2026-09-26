# Proposal — extend partial libc shim with copy/search imports

## Intent

Carry feature 033's bounded memmem/strcpy/strncpy services through the same real
ARM32 guest ELF path already prepared for the original seven libc primitives.

## Change

Extend the existing reproducible partial libc.so with three more SVC stubs,
extend the companion consumer with three wrappers, then extend the existing
namespace/provider/relocation/execution integration from seven functions to ten.

## Verification shape

Require all ten symbols to resolve from the shim object and all ten shim guest
addresses to appear as JUMP_SLOT relocation targets. Execute each wrapper with a
one-service-call ceiling and preserve the original test coverage.

## Non-goals

No new provider type or SONAME, no full libc claim, and no stateful libc
surfaces.
