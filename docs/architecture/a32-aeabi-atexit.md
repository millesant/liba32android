# ARM EABI __aeabi_atexit registration

Status: registration and registered-destructor finalization accepted; exact-head validation PASSed

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

## Registered-destructor finalization

Matching pending records can be finalized for one exact opaque DSO handle or
process-wide. Callbacks execute in reverse registration order, receive the
registered object in r0, and use the registered destructor word as an ARM/Thumb
function value.

Finalization is bounded by caller callback/instruction ceilings and an aligned
guest stack/normalized stop PC. Callback-count overflow is rejected before any
guest callback runs.

Each record is Pending, Complete, or Failed. Successful callbacks become
Complete and are never replayed. Invalid function addresses or guest execution
failures latch Failed so later calls cannot replay possible partial side
effects.

## Validation

All nine exact-head checks passed at
`1640ddc5e0958acbaeff4596f71338de65aa6081`, including the focused Linux
registered-finalizer regressions.

## Deliberate limits

Guest `__cxa_finalize`, association of DSO handles with link-map objects,
ordering registered callbacks against FINI_ARRAY/DT_FINI during dlclose/process
exit, shared-object reference-count ownership, mapping reclamation, and actual
unload remain follow-up lifecycle work.
