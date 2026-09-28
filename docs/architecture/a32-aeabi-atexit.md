# ARM EABI __aeabi_atexit registration

Status: registration/finalization accepted; lifecycle-scoped DSO association implemented

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

## Lifecycle-scoped object association

The registration service can optionally borrow finite caller-owned
object-to-DSO binding slots plus an ELF lifecycle execution context.

Lifecycle guest calls scope that context to the stable object index currently
executing. A non-zero DSO registration made in that scope therefore provides
direct ABI evidence for the association instead of requiring a guessed
`__dso_handle` symbol or load-bias rule.

Identical object/DSO registrations reuse one slot. Conflicting associations or
binding-capacity exhaustion return guest -1 atomically without appending the
registration. Calls outside a known lifecycle context remain normal
registrations but do not learn ownership.

The libdl close transaction can consume learned associations directly or use
the existing explicit binding table as fallback. Disagreement is rejected.

Learned slots are forgotten only after successful physical retirement of the
owning link-map object, allowing finite binding capacity to follow active
ownership while preserving bindings across failed-unload retries.

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

Association is learned only when a registration runs under known lifecycle
object provenance. Objects that never register `__aeabi_atexit`, arbitrary
runtime registrations outside lifecycle execution, and process-wide exit
ownership remain separate policy surfaces.
