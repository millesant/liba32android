# Proposal — export __errno from the partial libc shim

## Intent

Join feature 037's guest errno state to the real partial libc ELF path and prove
that a guest conversion error is visible through the ABI-level __errno pointer.

## Change

Append an __errno SVC stub, consumer declaration/wrapper, thirteenth symbol and
JUMP_SLOT requirement, and the errno-state service registry entry.

## End-to-end proof

Use overflowing strtol so feature 035 writes ERANGE through feature 037 into a
real guest slot. Then execute a separate guest wrapper that calls __errno and
dereferences the returned guest pointer.

## Non-goals

No Android TLS layout or additional libc API.
