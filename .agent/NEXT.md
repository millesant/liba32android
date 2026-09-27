# Next Work

Features 011 through 034 are DONE.

`035-a32-libc-integer-service` is IMPLEMENTED on `bleeding`; its exact-head
required checks are the immediate acceptance gate.

Once terminal success is observed, close 035 and integrate prepared feature 036,
which carries `atoi/strtol` through the real partial-libc shim path.

Prepared order after 035:

`036 -> 037 -> 038 -> 039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045`.

Preserve the accepted feature-033 memmem no-read ordering when integrating older
prepared trees until feature 039's broader Android-17 alignment lands.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
