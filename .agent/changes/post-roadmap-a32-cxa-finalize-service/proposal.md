# Proposal — guest __cxa_finalize service boundary

Android shared-object CRT teardown reaches registered C++ destructors through a
FINI_ARRAY function that calls __cxa_finalize(&__dso_handle). The accepted host
finalizer is therefore not sufficient for a realistic guest ELF destructor
path.

Expose one bounded guest service that translates the opaque guest selector into
the accepted registered-finalization operation. Keep symbol export,
service-aware FINI execution, link-map ownership, and dlclose/unmapping in later
slices.
