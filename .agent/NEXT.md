# Next Work

Features 011 through 039 are DONE.

`040-a32-libc-guest-heap` is IMPLEMENTED on `bleeding`; its exact-head
required checks are the immediate acceptance gate.

Once terminal success is observed, close 040 and integrate prepared feature 041,
which carries `malloc/calloc/realloc/free` through the real partial-libc
JUMP_SLOT -> SVC -> logical guest heap path.

Prepared order after 040:

`041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
