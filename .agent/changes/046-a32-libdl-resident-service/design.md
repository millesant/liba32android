# Design — resident-object ARM32 libdl compatibility

## Handles and ownership

The service borrows one persistent Elf32LinkMap, a finite caller-owned handle
span, and caller-owned guest scratch buffers. A handle is synthesized from a
caller-selected aligned 32-bit base plus slot offset. It is opaque to the guest
and is never a host pointer.

Repeated dlopen of the same resident object increments the same handle refcount.
Final dlclose recycles only the handle slot; link-map mappings and object state
remain resident.

## dlopen

Accept exactly one of RTLD_LAZY or RTLD_NOW. Null filename selects the first
link-map root. Non-null filename is copied through GuestMemory under a finite
name ceiling and must match exactly one resident object identity or SONAME.

No provider is called and no ELF mapping/relocation/lifecycle work occurs.

## dlsym

Ordinary synthetic handles search their selected object's graph-local closure
through lookup_elf32_graph_symbol. RTLD_DEFAULT begins with the first root and
then caller-recorded global roots. RTLD_NEXT is rejected with a pending error.

## dlerror

Errors use short host-owned messages. dlerror copies one pending message into
caller-owned guest scratch and clears it; no hidden guest allocation occurs.

## dladdr

Find the unique resident object whose loaded PT_LOAD segment contains the
address. dli_fname uses bounded resident SONAME or opaque identity and dli_fbase
uses the logical load bias. When dynamic symbol metadata is available, scan the
bounded indexed table and choose the nearest preceding defined symbol, preserving
the symbol's logical guest value (including Thumb bit for function pointers).

## Real shim

libdl.so exports dlopen/dlsym/dlclose/dlerror/dladdr as direct SVC stubs. A
freestanding consumer also depends on a separate provider DSO so the full
resident-object path is exercised after eager relocation.
