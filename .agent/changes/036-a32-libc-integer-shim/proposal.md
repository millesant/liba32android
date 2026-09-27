# Proposal — extend partial libc shim with atoi/strtol

## Intent

Carry feature 035's bounded integer conversion services through the same real
ARM32 ELF path as the prepared memory/string/copy/search partial libc.

## Change

Add atoi/strtol SVC stubs and consumer wrappers, extend symbol/JUMP_SLOT
requirements from ten to twelve, register the separate integer service, and run
normal real-wrapper conversions.

## Key proof

The strtol wrapper must write its guest end pointer through feature 035 and
return the expected signed ARM32 long value after resuming through the shim and
consumer.

## Non-goals

No guest __errno/TLS export, no additional conversion family, and no complete
libc claim.
