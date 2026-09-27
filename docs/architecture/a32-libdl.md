# ARM32 resident-object libdl compatibility

Status: feature 046 accepted; exact-head validation PASSed

## Goal

Provide the target-backed `libdl.so` ABI surface that can operate safely on
the project's existing persistent ELF32 link map without prematurely owning
Android filesystem/search or unload lifecycle.

The supplied FMOD image imports `dlopen`, `dlsym`, `dlclose`, and
`dlerror`. The supplied VLC ARMv7 set additionally imports `dladdr`.

## Guest ABI

The generated ARM32 `libdl.so` exports five direct SVC stubs:

- 0xBC — dlopen
- 0xBD — dlsym
- 0xBE — dlclose
- 0xBF — dlerror
- 0xC0 — dladdr

All guest handles/pointers remain logical 32-bit values. No host pointer is
published.

## Resident dlopen and handles

The service borrows a persistent `Elf32LinkMap` plus a finite caller-owned
handle table. `dlopen(name, RTLD_LAZY|RTLD_NOW)` matches only exact resident
SONAME/identity bytes. Null filename acquires the first root. Repeated opens of
the same resident object reuse one synthetic handle and increment its finite
refcount.

A missing object returns null and records a pending error. The service does not
invoke a dependency provider, open a path/APK, map an ELF, relocate it, or run
constructors.

`dlclose` only decrements/recycles the synthetic handle. The mapped object
persists; no FINI_ARRAY or unmap is performed.

## Symbol and address lookup

`dlsym(handle,name)` reuses bounded ELF32 graph lookup from the selected
resident root. `RTLD_DEFAULT` starts from the first process root and then
caller-recorded global roots. `RTLD_NEXT` is explicitly deferred.

`dladdr` locates the resident object whose loaded segment contains the guest
address, writes the ARM32 four-word `Dl_info` structure, copies a bounded
SONAME/identity into guest scratch, and scans the bounded dynamic symbol index
for the nearest preceding definition when available.

## Error lifetime

Errors are host-owned only until `dlerror` is called. `dlerror` copies one
pending bounded message into caller-provided guest scratch, returns that logical
guest pointer, and clears the pending error. A second call returns null until a
new error occurs.

## Real integration

The pinned-NDK fixture builds:

- `libdl.so` compatibility shim;
- `libfixture_dl_target.so` resident application DSO;
- `liba32android_libdl_consumer.so` importing both.

The integration loads the consumer into a persistent link map through an
application catalog plus namespace-gated platform `libdl.so`, eagerly
relocates it, proves direct resident-provider execution, then exercises guest
`dlopen -> dlsym -> dladdr -> dlclose -> failing dlsym -> dlerror`.

## Limits

Dynamic acquisition of a missing SONAME, Android search paths/APK extraction,
RTLD_GLOBAL/NOLOAD/NODELETE policy, RTLD_NEXT, relocation/constructor
transactions for newly loaded roots, persistent lifecycle called-state, actual
unload, and destructor execution remain separate work.
