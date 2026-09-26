# Additional shared ARM32 libc copy/search imports — 2026-09-26

## Scope

Using the same supplied artifacts and bounded undefined-symbol intersection
documented in
`shared-libc-memory-string-imports-2026-09-26.md`, both ARM32 targets also
import:

- `memmem`
- `strcpy`
- `strncpy`

The corrected unique-symbol counts are 106 FMOD undefined names, 572 VLC
`libvlc.so` undefined names, and 101 names in the intersection.

## Why these three

These functions extend the existing memory/string service without introducing
allocator ownership, file descriptors, thread state, dynamic-loader handles,
floating-point ABI handling, or process-global errno semantics.

`memmem` is bounded by explicit haystack/needle lengths. `strcpy` can be
bounded by the existing maximum string payload plus transfer ceiling.
`strncpy` is bounded by its explicit count and the transfer ceiling.

## Deferred common imports

The same intersection includes `atoi` and `strtol`, but integer conversion
raises additional questions around overflow and errno; it also includes
allocation, pthread, dlopen, I/O/stdio, socket, time, and libm functions with
larger state/ABI surfaces. Those remain separate changes.

## Limits

Static imports establish only that both binaries reference these symbols. This
does not prove these calls occur on a particular runtime path or that adding
them is sufficient for either target.
