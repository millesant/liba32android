# Compatibility spec delta — feature 032

Provide a reproducible freestanding ARM32 DSO with SONAME libc.so exporting only
memcpy, memset, memcmp, memchr, strlen, strcmp, and strncmp as the feature-030
shared SVC followed by bx lr.

Provide a borrowed exact-name libc.so catalog entry with stable identity and a
real ARM32 consumer importing all seven functions. Integration must acquire the
shim through feature-031 finite platform catalog plus feature-029 namespace
access, apply required JUMP_SLOT relocations, and execute every consumer wrapper
through feature-030.

Do not claim complete libc or supplied-application compatibility. Allocation,
threads, I/O, dynamic-loader, math, errno, process startup, signals, locale, and
all other libc behavior remain outside this feature.
