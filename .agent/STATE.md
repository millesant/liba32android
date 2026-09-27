# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `037-a32-libc-errno-state` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-036 are accepted.

Feature 036 passed all five exact-head checks at
`27495bb90f5ce425a1796daa73506051deaba158`, including the dedicated ARM32
partial-libc integer-shim integration.

Feature 037 is now integrated. It adds `A32LibcGuestErrnoState`: one
caller-selected logical guest errno slot, exact `__errno` pointer service at
SVC `0xAD`, and a failure-reporting GuestMemory-aware errno sink used by
`atoi/strtol`.

The accepted feature-033 memmem no-read correction remains preserved in the
integrated tree.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 036 at
`27495bb90f5ce425a1796daa73506051deaba158`:

- Linux A32 smoke `108519628987` — PASS.
- Android x86_64 address-space probe `108519628757` — PASS.
- Android arm64-v8a cross-build `108519628922` — PASS.
- ARM32 liblog shim integration `108519629540` — PASS.
- ARM32 libc memory string shim integration `108519629065` — PASS.

Feature 037 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`038 -> 039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Guest `__errno` ELF export, allocator, constructor/destructor lifecycle, ARM
EABI memory helpers, pthread/semaphore scheduling boundaries, libdl, libm,
Android filesystem/search, stable embedding API, and device execution remain
later work.
