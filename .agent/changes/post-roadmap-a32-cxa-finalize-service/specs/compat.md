# Compatibility spec delta — guest __cxa_finalize service

Expose private SVC 0xD3 as a guest __cxa_finalize boundary over the accepted
bounded atexit state.

r0==0 selects all pending records; nonzero r0 selects exact matching DSO
records. Successful delegated finalization returns Handled; any bounded
finalization error returns Failed and remains inspectable by the host.

This slice does not yet add the libc export or service-aware ELF destructor
execution.
