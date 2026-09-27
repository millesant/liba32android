# Compatibility spec delta — linked __cxa_finalize / service-aware FINI

Extend the partial libc target-backed surface to forty-one with
__cxa_finalize -> SVC 0xD3.

Allow ELF lifecycle execution to use an optional bounded host-service handler.
Lifecycle service-limit, unhandled, failed, and suspended outcomes are explicit
and preserve the failing SVC immediate.

The real ARM32 partial-libc consumer supplies one controlled FINI_ARRAY entry
that calls linked __cxa_finalize for an exact registered DSO word. Service-aware
FINI execution must run the matching registered guest destructor and return to
the lifecycle stop PC.

Unload ownership and mapping reclamation remain deferred.
