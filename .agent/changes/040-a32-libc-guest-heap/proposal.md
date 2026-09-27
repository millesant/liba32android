# Proposal — bounded ARM32 guest heap

## Intent

Implement the first allocator ownership surface shared by both supplied ARM32
targets without using host malloc pointers as guest pointers or copying Scudo
internals.

## APIs

Reserve private compatibility SVCs:

- 0xAE malloc
- 0xAF calloc
- 0xB0 realloc
- 0xB1 free

Use ARM32 size_t/pointer machine words directly from r0-r1 and return logical
guest pointers in r0.

## Storage model

The embedding supplies an already mapped writable guest arena and a finite
caller-owned metadata span. The heap suballocates the arena with deterministic
first-fit 16-byte placement.

## Failure model

Allocation exhaustion and calloc multiplication overflow return null and publish
Android ENOMEM through the existing guest errno sink. Invalid non-null
free/realloc pointers return Failed, matching the project's current fail-stop
boundary for allocator misuse without claiming Android signal/crash details.

## Non-goals

No guest ELF exports, Scudo internals, page mapping ownership, pthread locking,
aligned allocation, usable-size API, mallinfo/mallopt, or C++ operators.
