# Proposal — bounded A32 atoi/strtol service

## Intent

Implement the next two low-state libc imports shared by the supplied ARM32 FMOD
and VLC targets, but keep guest errno storage and guest ELF exports separate.

## Contract

Use private SVC 0xAB for atoi and 0xAC for strtol. Model ARM32 int/long as
signed 32-bit values, preserve guest logical pointers, and give strtol a real
guest end-pointer write.

## Errno

Borrow a caller-owned errno sink. Emit Android guest EINVAL for invalid base and
ERANGE for overflow/underflow; never mutate host errno.

## Bounds

The caller caps bytes examined. Parsing that cannot determine its result/endptr
within the cap or readable GuestMemory fails explicitly rather than scanning
unbounded memory.

## Non-goals

No guest shim export, __errno/TLS implementation, locale-aware ctype, unsigned
or 64-bit conversion, floating conversion, or stateful libc surfaces.
