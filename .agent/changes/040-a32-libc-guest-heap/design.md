# Design — bounded ARM32 guest heap

## Ownership

A32LibcGuestHeap borrows:

- A32LibcErrnoSink;
- A32LibcHeapOptions with non-zero arena begin and <=4GiB end;
- writable caller-owned A32LibcHeapBlock metadata storage.

The embedding keeps the whole arena mapped read/write for the heap lifetime.

## Placement

Each live block records base guest address, reserved aligned size, and requested
size. Active metadata is sorted by address. Allocation uses first fit and
16-byte alignment.

A zero-byte request reserves one minimum 16-byte block while recording requested
size zero.

## malloc/free

malloc returns a new logical guest base or null/ENOMEM.

free(0) succeeds without access. A non-null free must match an exact live base.
Unknown/interior/double-free input returns Failed.

## calloc

Compute r0*r1 in uint64. Product above UINT32_MAX returns null/ENOMEM.

Successful allocation zeroes exactly the requested product bytes through
GuestMemory using fixed-size host scratch storage. A GuestMemory failure rolls
back the new metadata.

## realloc

realloc(0,n) follows malloc. realloc(ptr,0) frees a valid live block and returns
null.

If the new aligned size fits the old reserved span, keep the pointer and update
requested size. Otherwise allocate a new span first, copy the minimum requested
payload in bounded chunks, then release old metadata.

New-allocation failure returns null/ENOMEM and preserves old allocation.
GuestMemory copy failure rolls back new metadata and preserves old allocation.

## Concurrency

Feature 040 is single-context and externally serialized. Thread-safe heap access
is deferred until the pthread/runtime concurrency model exists.
