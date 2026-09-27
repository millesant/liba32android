# Design — partial libc allocator shim extension

## Guest shim

Include the preprocessor-safe feature-040 allocator SVC definitions in the
existing ARM assembly fixture and add four minimal exported functions:
`malloc`, `calloc`, `realloc`, and `free`. Each function executes exactly
its shared SVC and returns with `bx lr`.

## Consumer and relocations

The freestanding consumer declares the allocator quartet and exposes one
noinline wrapper per function. With builtins disabled and the existing
`libc.so` dependency, the consumer retains ordinary eager
`R_ARM_JUMP_SLOT` imports. Integration grows the required symbol/relocation
set from thirteen to seventeen without changing provider identity or namespace
policy.

## Host service

The test maps one writable guest page as the heap arena, provides finite
caller-owned `A32LibcHeapBlock` metadata, and constructs one
`A32LibcGuestHeap` using the same `A32LibcGuestErrnoState` already shared by
integer conversion and `__errno`.

Execution covers guest malloc, realloc, calloc, and free. Realloc must preserve
a staged payload and calloc must expose zeroed requested bytes. Returned
allocator addresses are checked to remain inside the mapped guest heap arena.

## CI

The dedicated pinned-NDK workflow inspects all seventeen shim symbols and all
seventeen eager consumer JUMP_SLOT relocations, then requires integration
evidence reporting seventeen required jump slots and seventeen completed
service calls.
