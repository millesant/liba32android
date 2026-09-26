# Proposal — additional bounded libc copy/search services

## Intent

Extend the prepared libc memory/string service with the next three shared
FMOD/VLC imports that remain purely bounded guest-memory operations: memmem,
strcpy, and strncpy.

## Protocol

Append shared private SVC IDs 0xA8, 0xA9, and 0xAA without changing the existing
0xA1-0xA7 assignments.

## Safety boundary

memmem is bounded by its two explicit lengths. strcpy must observe a NUL inside
the existing string ceiling and buffers the source before destination mutation.
strncpy is bounded by its explicit count/transfer ceiling and preserves standard
padding/non-termination behavior.

## Non-goals

Do not add atoi/strtol yet because overflow/errno semantics need their own
contract. Do not add allocator, pthread, I/O, dlopen, libm, or guest-shim
exports in this change.
