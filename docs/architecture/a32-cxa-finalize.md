# Guest __cxa_finalize service boundary

Status: implemented; exact-head validation pending

## Why this boundary exists

Android shared-object CRT code installs a FINI_ARRAY destructor that calls
`__cxa_finalize(&__dso_handle)`. The Android linker processes FINI_ARRAY in
reverse order before DT_FINI. A future realistic dlclose transaction therefore
needs a guest-callable finalization boundary rather than only a host helper.

## Guest ABI

Private SVC `0xD3` represents:

`void __cxa_finalize(void* dso_handle)`

Guest r0 is treated as an opaque logical word. Zero selects process-wide
finalization; non-zero selects an exact registered DSO handle. No host pointer
conversion or automatic link-map lookup occurs.

## Service composition

`A32CxaFinalizeService` borrows the accepted
`A32AeabiAtexitService` registration/finalization state and fixed bounded
finalization options.

The service delegates to the accepted reverse-order finalizer. On success the
SVC is Handled and guest execution may resume after the void call. On bounded
finalization failure the service returns Failed and retains the detailed
`A32AeabiFinalizeResult` for host diagnostics.

A focused real ARM service-registry regression executes SVC 0xD3, finalizes an
exact DSO registration, enters its guest destructor, verifies the original
registered object in r0, and observes once-only completion.

## Deferred

This slice does not yet add the `__cxa_finalize` symbol to the generated
partial libc shim. It also does not make FINI_ARRAY execution service-aware,
bind `__dso_handle` values to link-map objects, own dlclose reference counts,
or unmap objects. Those are the next transaction-building steps.
