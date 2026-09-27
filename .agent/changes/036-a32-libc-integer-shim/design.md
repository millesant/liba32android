# Design — extend partial libc shim with atoi/strtol

## Guest exports

The partial libc assembly includes both shared service-ID headers and appends:

- atoi -> svc #0xAB; bx lr
- strtol -> svc #0xAC; bx lr

## Consumer

Declare int atoi(const char*) and long strtol(const char*, char**, int), then add
fixture_atoi and fixture_strtol wrappers. The ARMv7 target is ILP32 so long
returns in r0.

## Integration

Resolve twelve shim symbols and require each final guest address in a JUMP_SLOT
write. Instantiate the existing memory/string service plus a recording errno
sink and A32LibcIntegerService. Register ten IDs to the first handler and two to
the second.

Stage "123" and " -0x10z" in guest memory. Require fixture_atoi to return 123.
Require fixture_strtol with base 0 to return -16, write endptr to the guest
address of 'z', and leave the recording errno sink untouched.

## Dedicated workflow

Inspect twelve exports/import relocations and require integration evidence for
twelve required jump slots and twelve completed service calls.

## Non-goals

No overflow-path guest errno integration, __errno/TLS export, additional libc
symbols, or provider changes.
