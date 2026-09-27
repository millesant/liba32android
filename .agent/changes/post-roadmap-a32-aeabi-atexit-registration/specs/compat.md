# Compatibility spec delta — ARM EABI atexit registration

Expose __aeabi_atexit through private SVC 0xD2 with exact AAPCS32
object/destructor/DSO arguments and finite caller-owned registration storage.

Return 0 on append success. Return ARM32 -1 when storage is full without
mutating previous records. Both outcomes are handled guest results.

Extend the current partial libc shim/consumer/integration to forty symbols and
prove exact registration. Registered-destructor execution and __cxa_finalize
remain deferred.
