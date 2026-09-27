# Bounded A32 guest heap service

Status: feature 040 implementation prepared OFF-REF; exact-head validation NOT RUN

## Boundary

`A32LibcGuestHeap` implements the four allocator calls shared by the supplied
ARM32 FMOD/VLC targets without exposing host allocation addresses:

- `malloc` — SVC `0xAE`;
- `calloc` — SVC `0xAF`;
- `realloc` — SVC `0xB0`;
- `free` — SVC `0xB1`.

The caller supplies an already mapped writable guest arena
`[begin,end_exclusive)`, finite writable metadata storage, and an
`A32LibcErrnoSink`. The heap owns none of those resources.

## Allocation model

The heap uses deterministic first-fit placement over 16-byte aligned logical
guest addresses. A zero-byte allocation reserves one minimum aligned block but
retains requested size zero.

Metadata is kept sorted by logical guest address. Free removes exactly one live
base pointer and makes the span reusable; `free(0)` is a no-op. Unknown
non-null free/realloc pointers return `Failed` rather than being ignored.

## calloc

ARM32 `size_t` inputs are 32-bit. Multiplication is performed in 64 bits.
Product above `UINT32_MAX` returns null plus guest ENOMEM.

Successful non-zero calloc zeros exactly the requested bytes through
`GuestMemory` using a fixed bounded host scratch chunk. A guest-memory failure
rolls back the allocation metadata and returns `Failed`.

## realloc

`realloc(0,size)` follows malloc. `realloc(ptr,0)` frees the exact live block
and returns null.

If the new aligned size fits the current reserved span, the pointer remains
stable. Otherwise the heap allocates a new span before touching the old one,
copies `min(old_requested,new_requested)` bytes through fixed-size
`GuestMemory` scratch storage, then frees the old metadata entry.

If new allocation fails, null/ENOMEM is returned and the old allocation remains
live. If guest copying fails, the new metadata is rolled back and the old
allocation remains live.

## Errno and ownership

Out-of-memory and calloc-overflow conditions publish Android ENOMEM=12 through
the existing caller-owned guest errno sink. If required errno publication
fails, the service returns `Failed`.

The arena must remain mapped read/write for the heap lifetime. The feature does
not map/unmap pages itself, return host pointers, or depend on Scudo internals.

## Threading and non-goals

The heap is a single-context bounded allocator and performs no locking. Future
pthread work must serialize or provide per-process heap synchronization.

No partial-libc guest shim exports are added in feature 040; those belong in the
next integration slice. No aligned allocation, malloc_usable_size,
mallinfo/mallopt, Scudo quarantine/tagging, C++ new/delete, or host-OS page
release is introduced.
