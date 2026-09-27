# Design — linked __cxa_finalize plus service-aware ELF FINI

The partial libc shim gains a direct SVC 0xD3 stub and the freestanding consumer
gains a normal import, wrapper, exact DSO-handle symbol, registered destructor,
and one explicit .fini_array pointer.

Lifecycle execution options gain an optional borrowed A32HostServiceHandler and
a per-lifecycle-function service-call ceiling. If absent, execution continues
to call the CPU adapter directly. If present, the existing bounded service
dispatcher runs each lifecycle function and must reach the same normalized stop
PC.

Service-specific failures are projected into lifecycle error classes and retain
the failing SVC immediate. Persistent lifecycle copies that identity when it
latches an object Failed.

The real integration registers the consumer's guest destructor and DSO symbol,
executes the one FINI_ARRAY entry with the ordinary compatibility service
registry, verifies SVC 0xD3 finalization and guest destructor side effect, then
invokes the linked finalize wrapper again to prove once-only state.
