# Compatibility spec delta — feature 034

Extend the prepared partial ARM32 libc.so and consumer from seven to ten
functions by adding memmem, strcpy, and strncpy at the feature-033 shared SVC
IDs 0xA8-0xAA.

The real integration must resolve all ten imports from the shim, require every
shim guest address as an eager JUMP_SLOT target, register all ten SVC IDs, seed
r3 for memmem's fourth argument, and execute the three new wrappers with
correct guest-memory and return-pointer results.

Keep the existing libc.so SONAME/catalog identity and namespace/provider path.
Do not add broader libc behavior or claim application compatibility.
