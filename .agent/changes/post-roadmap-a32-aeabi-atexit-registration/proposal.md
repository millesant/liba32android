# Proposal — bounded ARM EABI atexit registration

The supplied FMOD library imports __aeabi_atexit. That symbol was deliberately
deferred until persistent lifecycle state existed.

Add a finite caller-owned registration service and extend the existing partial
libc shim rather than inventing a second libc identity. Preserve Android's
observable ABI: __aeabi_atexit delegates registration of destructor/object/DSO
and returns 0 on success or -1 on registration failure.

Do not execute registered destructors yet; finalization belongs with the next
lifecycle/unload transaction slice.
