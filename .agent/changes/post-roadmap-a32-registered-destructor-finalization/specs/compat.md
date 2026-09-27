# Compatibility spec delta — registered-destructor finalization

Extend accepted __aeabi_atexit registration records with Pending/Complete/Failed
state and a bounded host finalization operation.

Finalize matching callbacks in reverse registration order, pass the exact
registered object in r0, honor ARM/Thumb destructor bit0, and require caller
stack/stop/instruction/callback ceilings. Preflight callback count before guest
execution. Complete callbacks are once-only; invalid or failed guest callbacks
latch Failed and are never replayed.

Guest __cxa_finalize, DSO/link-map ownership, dlclose/process-exit ordering,
reference-counted unload, and unmapping remain deferred.
