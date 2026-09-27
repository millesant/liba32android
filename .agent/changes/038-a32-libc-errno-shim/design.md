# Design — export __errno from the partial libc shim

## Guest ABI

Append:

__errno:
    svc #0xAD
    bx lr

The consumer declares int* __errno(void) and fixture_errno_read returns
*__errno().

## Integration

Configure one mapped guest errno slot and construct A32LibcGuestErrnoState with
that logical address. Pass the same state as A32LibcIntegerService's errno sink
and register it for exact SVC 0xAD.

Extend symbol/JUMP_SLOT requirements to thirteen entries.

Replace the normal real strtol input with "2147483648x". Require:

- strtol r0 == INT32_MAX;
- endptr == address of 'x';
- guest errno slot == ERANGE (34).

Then run fixture_errno_read. Its guest call crosses the __errno shim/service,
returns the logical slot, dereferences it in guest code, and must return 34.

## CI

Inspect thirteen shim symbols/import relocations and require evidence for
thirteen JUMP_SLOT targets and thirteen completed service calls.

## Non-goals

No thread creation, TLS block emulation, cached thread selection, or other libc
symbols.
