# Compatibility spec delta — feature 040

Add bounded ARM32 malloc/calloc/realloc/free host services at private SVC IDs
0xAE-0xB1.

The heap borrows a caller-owned already mapped writable guest arena, finite
metadata span, and guest errno sink. It returns only logical 32-bit guest
addresses and uses deterministic 16-byte aligned first-fit placement.

malloc(0) and calloc with a zero product receive a minimum internal allocation
when capacity exists. calloc multiplication overflow and allocation exhaustion
return null and publish Android ENOMEM=12.

realloc(nullptr,n) follows malloc. realloc(ptr,0) frees an exact live pointer
and returns null. Growth preserves the minimum requested payload and failed
growth preserves the original allocation.

free(nullptr) is a no-op. Unknown non-null free/realloc pointers return Failed.

Do not add guest libc ELF exports, Scudo internals, page mapping ownership,
thread synchronization, aligned allocation, malloc_usable_size, mallinfo,
mallopt, or C++ allocation operators in feature 040.
