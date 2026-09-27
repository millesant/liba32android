# Compatibility spec delta — feature 041

Extend the prepared partial ARM32 `libc.so` and consumer with
`malloc/calloc/realloc/free` using shared feature-040 SVC IDs `0xAE-0xB1`.

Real integration must resolve seventeen partial-libc imports, require seventeen
eager JUMP_SLOT targets, bind the allocator quartet to one
`A32LibcGuestHeap`, and execute all four guest wrappers through the existing
namespace-gated finite platform catalog.

The heap uses only an already mapped writable logical guest arena plus finite
caller-owned metadata and the shared guest errno state. Realloc payload
preservation and calloc zeroing are observed in the real fixture path.

No thread/TLS, constructor/destructor lifecycle, aligned allocation, I/O,
dynamic-loader, math, startup, signal, locale, device-execution, or full-libc
claim is introduced.
