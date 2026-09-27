# Guest __cxa_finalize service boundary

Status: accepted; exact-head validation PASSed

## Why this boundary exists

Android shared-object CRT code installs a FINI_ARRAY destructor that calls
`__cxa_finalize(&__dso_handle)`. The Android linker processes FINI_ARRAY in
reverse order before DT_FINI. A realistic dlclose path therefore needs a
guest-callable finalization boundary rather than only a host helper.

## Guest ABI

Private SVC `0xD3` represents `void __cxa_finalize(void* dso_handle)`.

Guest r0 is an opaque logical word. Zero selects process-wide finalization;
non-zero selects an exact registered DSO handle. No host pointer conversion or
automatic link-map lookup occurs.

## Service composition

`A32CxaFinalizeService` borrows the accepted
`A32AeabiAtexitService` registration/finalization state and bounded options.

The service delegates to reverse-order registered finalization. Success is
Handled so guest execution resumes after the void call. Failure is Failed and
retains the detailed `A32AeabiFinalizeResult` for host diagnostics.

## Validation

All nine exact-head checks passed at
`fbd2e70c6b92bfe7b4242a7b69c083fdbf2de131`, including a real ARM
service-registry regression that selects one DSO, executes its registered guest
destructor with the exact object argument, and observes Complete state.

## Deferred

The `__cxa_finalize` partial-libc export, service-aware FINI execution,
DSO/link-map ownership, dlclose ownership, reference-counted unload, and
mapping reclamation remain follow-up work.
