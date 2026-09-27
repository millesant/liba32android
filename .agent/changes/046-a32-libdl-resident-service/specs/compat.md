# Compatibility spec delta — feature 046

Add private SVC IDs 0xBC-0xC0 for dlopen/dlsym/dlclose/dlerror/dladdr.

The service operates only on a caller-owned persistent Elf32LinkMap. Synthetic
handles are finite logical guest values. dlopen may refcount an exact already
resident SONAME/identity; it does not acquire or map a missing object. dlsym
uses bounded resident symbol lookup with RTLD_DEFAULT support and explicit
RTLD_NEXT rejection. dlclose releases only handle state. dlerror uses bounded
caller-owned guest scratch and clears after one read. dladdr writes ARM32
Dl_info using logical guest module/symbol addresses.

The generated libdl.so exports all five functions as direct SVC stubs. Real
integration combines namespace-gated platform libdl with a separate
application-resident provider and exercises all five ABI paths.

Filesystem/APK search, missing-object dlopen acquisition, RTLD_GLOBAL/NOLOAD/
NODELETE, RTLD_NEXT, unload, FINI_ARRAY on close, and persistent lifecycle
called-state remain deferred.
