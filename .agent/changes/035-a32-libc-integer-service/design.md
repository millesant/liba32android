# Design — bounded A32 atoi/strtol service

## ABI

atoi:
- r0 input char*
- r0 signed 32-bit result

strtol:
- r0 input char*
- r1 guest char** endptr or zero
- r2 signed int base
- r0 signed 32-bit long result

ARM32 Android is ILP32, so int and long are represented by one 32-bit register.

## Parser

Validate base before reading input. ASCII parsing then skips C whitespace,
handles one sign, recognizes guarded 0x prefixes for base 0/16 and current
bionic 0b prefixes for base 0/2, chooses octal/decimal for remaining base 0,
and consumes digits through base 36.

Use a uint64 magnitude with signed limits 2147483647/2147483648. On overflow,
stop accumulating but continue consuming valid digits so endptr is correct.

No digits means zero and endptr == original input.

## Errno and publication

The service owns no errno state. A caller-owned A32LibcErrnoSink receives guest
EINVAL=22 or ERANGE=34 only when bionic would set errno.

Parsing is side-effect-free. For strtol, a requested endptr is written as one
little-endian 32-bit guest address before errno/result publication. If the write
fails, return Failed.

## Bounds

Every byte read consumes one position inside max_parse_bytes. Running out of
the bounded readable window before an invalid/terminating byte is observed is
Failed.

## Verification

Direct tests cover bases/prefixes/endptr/errors/bounds, and an ARM svc #0xAB;
bx lr fixture proves atoi through the exact-SVC registry and resumable
dispatcher.
