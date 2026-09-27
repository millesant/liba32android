# Next Work

Features 011 through 040 are DONE.

`041-a32-libc-allocator-shim` is IMPLEMENTED on `bleeding`; its exact-head
required checks are the immediate acceptance gate.

Once terminal success is observed, close 041 and integrate prepared feature 042.

Prepared order after 041:

`042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
