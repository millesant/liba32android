# Design — ARM EABI atexit registration

## Guest ABI

Reserve private SVC 0xD2.

r0 = object
r1 = destructor function value
r2 = DSO handle

The service records all three raw logical guest words exactly.

## Capacity and result

Registration storage is a caller-owned fixed span plus a count. Success appends
one record and returns r0=0. Full storage returns r0=0xffffffff and leaves the
record set unchanged. Capacity exhaustion remains Handled because -1 is the
guest-visible __cxa_atexit failure result.

## Integration

The existing partial libc.so gains __aeabi_atexit as a direct SVC stub. Its
consumer gains one wrapper/import. The existing real integration verifies forty
resolved JUMP_SLOT targets, registers the new service, executes the wrapper, and
checks exact registration state.

## Deferred finalization

This slice intentionally has no callback execution or __cxa_finalize API. DSO
matching, reverse-order finalization, dlclose/process-exit timing, and lifecycle
state integration remain later work.
