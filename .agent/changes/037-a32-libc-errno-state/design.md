# Design — guest errno state and __errno service

## ABI

Reserve SVC 0xAD for int* __errno(void). The service returns one configured
logical guest address in r0.

## Errno sink

Change A32LibcErrnoSink::set_errno to:

bool set_errno(GuestMemory&, int32_t)

A32LibcGuestErrnoState implements the method by one little-endian four-byte
write into its configured slot. Null slot or GuestMemory write failure returns
false.

A32LibcIntegerService treats a false required errno publication as Failed.

## __errno handler

Unknown IDs remain Unhandled. A matching request with null/wrapping slot is
Failed; otherwise r0 receives the logical guest slot. The handler does not touch
host errno or return a host address.

## Guest semantics

Because conversion errors write the actual guest slot immediately, code that
caches a prior __errno result still observes later EINVAL/ERANGE through guest
memory.

## Threading

The state has no thread discovery. Future guest threading must register/select a
different state/slot for each guest thread.

## Verification

Direct tests write/read ERANGE through GuestMemory, resolve the __errno pointer,
reject null configuration, and keep integer conversion/regression consumers
compiling against the strengthened sink.
