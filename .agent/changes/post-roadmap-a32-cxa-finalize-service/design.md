# Design — guest __cxa_finalize service boundary

Reserve private SVC 0xD3. Guest r0==0 means process-wide finalization;
otherwise r0 is an exact opaque DSO selector.

A32CxaFinalizeService borrows A32AeabiAtexitService and immutable bounded
finalization options. It delegates directly to the existing finalizer.

Success returns Handled because __cxa_finalize is void and guest execution
should resume. Any finalization error returns Failed; the exact finalization
result is retained for host diagnostics.

The direct ARM regression places SVC 0xD3 in guest code, registers a guest
destructor through the accepted state, dispatches the SVC through the ordinary
host-service registry, and verifies the destructor's r0 object side effect.

No link-map lookup, DSO ownership, ELF FINI service dispatch, or unmapping is
introduced.
