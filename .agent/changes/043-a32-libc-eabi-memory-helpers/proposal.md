# Proposal — ARM EABI memory helpers

Close the next target-backed libc gap by adding overlap-safe plain memmove and
the twelve bionic ARM EABI memory helpers observed in the supplied VLC ARMv7
native set.

Reuse the existing bounded memory/string service wherever the ABI permits.
Only memmove needs a new private host SVC. memcpy/memmove EABI variants dispatch
directly; memset variants reorder ARM EABI arguments; memclr variants translate
to zero-valued memset.

Extend the same partial libc fixture/provider rather than creating another DSO
identity.

Do not add __aeabi_atexit, C++ destructor registration, pthread/TLS, I/O, libdl,
libm, or full-libc claims in this slice.
