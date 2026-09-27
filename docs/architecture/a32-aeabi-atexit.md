# ARM EABI __aeabi_atexit registration

Status: accepted; exact-head validation PASSed

## Boundary

The supplied FMOD ARM32 image imports `__aeabi_atexit`. Android's ARM ABI
routes this helper to C++ destructor registration rather than a stateless memory
primitive.

The compatibility service reserves private SVC `0xD2` and records r0 object,
r1 destructor function value, and r2 DSO handle as opaque logical guest values.

## Registration state

`A32AeabiAtexitService` borrows finite caller-owned record storage.
Successful calls append in order and return guest r0 = 0. Capacity exhaustion
returns guest r0 = -1 without changing existing records.

## Partial libc path

The accepted ARM32 `libc.so` shim exposes `__aeabi_atexit` as its fortieth
target-backed function. Exact-head real integration resolves the eager
JUMP_SLOT, executes the wrapper, and verifies exact object/destructor/DSO state.

## Validation

All nine exact-head checks passed at
`3272ec52fa54208a9435f3991c92172fd71b2be0`, including the focused Linux
regressions and the forty-symbol ARM32 partial-libc integration.

## Deliberate limits

Registered callbacks are not executed in this slice. `__cxa_finalize`,
process-exit finalization, per-DSO reverse-order execution, association of DSO
handles with link-map objects, dlclose-driven finalization, and unmapping remain
follow-up lifecycle/unload work.
