# Proposal — partial ARM32 libc memory/string shim/provider

## Intent

Join feature 030's verified-by-design host-service slice to the real ARM32 ELF
path using the finite platform catalog prepared by feature 031.

## Shim

Build a freestanding ARM-mode libc.so exporting only memcpy, memset, memcmp,
memchr, strlen, strcmp, and strncmp. Every symbol is exactly SVC + bx lr using
the feature-030 shared service IDs.

## Consumer

Build a second freestanding DSO with compiler builtins disabled and one exported
wrapper per function. Linking against the generated shim creates a real
DT_NEEDED libc.so edge and eager function relocations.

## End-to-end proof

Load through application provider -> feature-031 platform catalog, with
feature-029 namespace access exposing only libc.so. Relocate the consumer,
execute every wrapper, and require all seven calls to reach feature-030 through
the exact SVC registry and resume to the requested stop PC.

## Non-goals

This is not a complete libc and is not sufficient to load FMOD/VLC. No
allocator/thread/I/O/dlopen/math/process-global behavior is added.
