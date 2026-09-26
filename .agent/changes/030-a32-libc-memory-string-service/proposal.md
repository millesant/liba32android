# Proposal — bounded A32 libc memory/string service

## Intent

Implement the first libc host-service slice justified by symbols shared by the
supplied ARM32 FMOD and VLC targets, while avoiding allocator/thread/I/O/dlopen
state.

The chosen primitives are memcpy, memset, memcmp, memchr, strlen, strcmp, and
strncmp.

## Protocol

Reserve private compatibility SVC immediates 0xA1-0xA7 in one
preprocessor-safe header so a later guest libc shim can consume the same
definitions.

One host-service handler recognizes all seven IDs. A future registry may map
each exact ID to the same handler instance.

## Bounds

The caller supplies separate finite memory-transfer and string ceilings. Every
logical guest range must remain inside 32-bit address space. Guest access
failures become Failed before a successful result is published.

## Layering

This change is host-service only. It deliberately follows the successful
log-write pattern: service first, guest ELF shim/provider later.

## Non-goals

No guest libc.so, malloc/free, strcpy/strncpy, strtol, stdio, file/socket I/O,
pthread synchronization, dlopen/dlsym, math, errno, or process-global libc
state.
