# Compatibility spec delta — feature 036

Extend the prepared partial ARM32 libc.so/consumer with atoi and strtol at
shared feature-035 service IDs 0xAB/0xAC.

Real integration must resolve twelve shim imports, require twelve JUMP_SLOT
targets, route the ten memory/string/copy/search IDs and two integer IDs to
their respective handlers, and execute normal atoi/strtol wrapper calls.
strtol must publish its logical guest end pointer correctly.

Keep guest __errno/TLS and all other libc symbols outside this feature.
