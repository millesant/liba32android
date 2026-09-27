# ARM EABI __aeabi_atexit registration

Status: registration accepted; registered-destructor finalization implemented, exact-head validation pending

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

## Registered-destructor finalization

The post-registration follow-up executes pending callbacks in reverse
registration order. A host caller may select one exact opaque DSO handle or all
records. The callback receives the exact registered object in r0 and uses the
registered destructor word as its ARM/Thumb entry point.

Finalization is bounded by caller-selected callback and instruction ceilings,
plus the existing aligned guest stack/normalized stop-PC contract. Callback
ceilings are preflighted before guest execution.

Each record is Pending, Complete, or Failed. Successful callbacks become
Complete and are never replayed. Invalid function addresses or guest
execution failures latch Failed so later calls cannot replay possible partial
side effects.

## Deliberate limits

Guest `__cxa_finalize` export, association of DSO handles with link-map
objects, ordering registered callbacks against FINI_ARRAY/DT_FINI during
dlclose/process exit, shared-object reference-count ownership, mapping
reclamation, and actual unload remain follow-up lifecycle work.
