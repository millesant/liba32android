# Next Work

Features 011 through 036 are DONE.

`037-a32-libc-errno-state` is IMPLEMENTED on `bleeding`; its exact-head
required checks are the immediate acceptance gate.

Once terminal success is observed, close 037 and integrate prepared feature 038,
which exports `__errno` through the real partial-libc shim and proves the same
guest slot receives strtol ERANGE.

Prepared order after 037:

`038 -> 039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

Preserve the accepted feature-033 memmem correction when integrating older
prepared trees until feature 039's Android-17 alignment batch lands.

## Local-machine validation note

If a later step materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.

## Independent follow-ups

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
