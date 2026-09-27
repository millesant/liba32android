# Design — registered-destructor finalization

## State

Each registered record retains the exact object, destructor, and DSO words plus
one internal status: Pending, Complete, or Failed.

Registration remains append-only. Finalization does not recycle capacity.

## Selection and bounds

The host caller selects either one exact DSO word or all records. Matching
Pending callbacks are counted in a reverse preflight. Failed matching records
return InvalidRecordState. If the pending count exceeds max_callbacks, return
CallbackLimitExceeded before executing guest code.

## Guest call ABI

Callbacks execute newest-first. The destructor word uses bit 0 for Thumb and
the normalized remainder as entry PC. r0 receives the exact registered object.
The caller supplies an aligned guest stack top, normalized return PC, and finite
instruction ceiling.

## Failure

Successful callbacks become Complete. Invalid callback addresses and
memory/CPU/instruction-limit failures mark the current record Failed before
returning. Earlier successfully completed callbacks remain Complete.

This conservative state machine prevents retrying a callback that may have
already changed guest state before failing.

## Deferred transaction ownership

No guest __cxa_finalize wrapper is added. No DSO handle is mapped to a link-map
object yet. dlclose/process-exit ordering against registered callbacks and
FINI_ARRAY/DT_FINI, refcount ownership, and mapping reclamation remain separate.
