# Compatibility spec delta — feature 038

Extend the prepared partial ARM32 libc.so/consumer with __errno at shared
feature-037 SVC 0xAD.

Real integration must resolve thirteen imports, require thirteen JUMP_SLOT
targets, use the same A32LibcGuestErrnoState as __errno service and integer errno
sink, cause a real strtol overflow to publish ERANGE into guest memory, and
prove a guest __errno dereference reads that value.

Do not implement Android TLS layout, guest thread selection, or additional libc
symbols in this feature.
