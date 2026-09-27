# Proposal — bounded registered-destructor finalization

The accepted __aeabi_atexit slice records exact destructor/object/DSO triples
but deliberately does not execute them.

Add a bounded host finalization operation over the same caller-owned records.
Support exact per-DSO and process-wide selection, reverse registration order,
once-only completion, and conservative failure latching.

Do not yet expose __cxa_finalize to guest code or decide dlclose ownership.
Those require DSO-handle/link-map association and a transaction ordering
contract with ELF FINI_ARRAY/DT_FINI.
