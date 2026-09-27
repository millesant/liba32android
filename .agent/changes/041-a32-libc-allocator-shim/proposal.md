# Proposal — partial libc allocator shim extension

Extend the existing reproducible ARM32 partial `libc.so` fixture with
`malloc`, `calloc`, `realloc`, and `free` stubs using feature 040's shared
SVC IDs. Extend the freestanding consumer and real integration so the allocator
quartet crosses the existing eager relocation and namespace-gated provider path
before entering one bounded `A32LibcGuestHeap`.

The integration must keep all pointers logical to the guest, reuse the existing
guest errno state, prove realloc payload preservation and calloc zeroing, and
leave the current partial-libc identity and SONAME unchanged.

Non-goals are pthread/TLS, constructor/destructor lifecycle, aligned allocation,
stdio/file/socket I/O, libdl, libm, process startup, signals, locale, or a claim
of complete Android libc compatibility.
