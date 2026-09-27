# Next Work

Features 011 through 037 are DONE.

`038-a32-libc-errno-shim` is IMPLEMENTED on `bleeding`; its exact-head
required checks plus dedicated ARM32 partial-libc integration are the immediate
acceptance gate.

Once terminal success is observed, close 038 and integrate prepared feature 039,
the Android-17 compatibility-alignment batch.

Prepared order after 038:

`039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
