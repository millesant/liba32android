# Next Work

Features 011 through 038 are DONE.

`039-android17-release-alignment` is IMPLEMENTED on `bleeding`; its
exact-head applicable checks are the immediate acceptance gate.

Once terminal success is observed, close 039 and integrate prepared feature 040,
the bounded logical ARM32 guest heap for `malloc/calloc/realloc/free`.

Prepared order after 039:

`040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
