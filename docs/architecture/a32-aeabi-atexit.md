# ARM EABI __aeabi_atexit registration

Status: integrated; exact-head validation pending

## Boundary

The supplied FMOD ARM32 image imports `__aeabi_atexit`. Android's ARM ABI
routes this helper to C++ destructor registration rather than a stateless memory
primitive.

The compatibility service reserves private SVC `0xD2` and records:

- r0 — object argument;
- r1 — destructor function value;
- r2 — DSO handle.

All three remain opaque logical 32-bit guest values.

## Registration state

`A32AeabiAtexitService` borrows a finite caller-owned record span. Successful
calls append in registration order and return guest r0 = 0. Capacity exhaustion
returns guest r0 = -1 without changing existing records.

No guest memory read is needed during registration and no host pointer is
published.

## Partial libc path

The ARM32 `libc.so` shim adds a direct `__aeabi_atexit` SVC stub.
The freestanding consumer imports it normally, producing the fortieth eager
JUMP_SLOT in the partial-libc fixture. Real integration executes that wrapper
through the service registry and verifies exact object/destructor/DSO state.

## Deliberate limits

Registered callbacks are not executed in this slice. `__cxa_finalize`,
process-exit finalization, per-DSO reverse-order execution, association of DSO
handles with link-map objects, dlclose-driven finalization, and unmapping remain
follow-up lifecycle/unload work.
