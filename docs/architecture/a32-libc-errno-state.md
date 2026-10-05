# A32 guest errno state and __errno service

Status: accepted current architecture; bounded guest errno service implemented

## Shared service

Feature 037 reserves private SVC `0xAD` for `__errno`.

`A32LibcGuestErrnoState` is configured with one non-zero logical guest address
for a four-byte errno slot. Its service returns that address in r0 without
exposing a host pointer.

## Errno sink convergence

The feature-035 `A32LibcErrnoSink` contract is strengthened to receive
`GuestMemory` and report publication success.

The guest errno state implements that sink by writing the signed errno bits
little-endian into its configured guest slot. This means strtol overflow/base
errors can update the same memory that guest code later reaches through
`__errno`, including code that caches the returned guest pointer.

If the configured slot is null or the GuestMemory write fails, integer
conversion returns `Failed` rather than silently publishing only r0.

## Thread boundary

One state object represents one current guest errno slot. Future guest threading
must select a distinct state/slot per guest thread; feature 037 does not infer
threads or emulate Android TLS layout.

## Non-goals

No pthread/TLS implementation, `__get_tls`, host errno aliasing, or other
thread-local libc data is introduced.
