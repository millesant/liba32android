# Proposal — linked __cxa_finalize plus service-aware ELF FINI

The accepted guest finalization service is not yet reachable from ordinary
linked libc code, and the ELF lifecycle executor currently treats any SVC as a
terminal CPU exception.

Add the libc export and a bounded optional service dispatcher to lifecycle
execution. Prove the Android teardown shape end to end with a controlled real
ARM32 FINI_ARRAY entry that calls __cxa_finalize for an exact registered DSO
word.

Keep DSO/link-map ownership and actual dlclose/unmapping separate.
