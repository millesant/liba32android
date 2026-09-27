# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `039-android17-release-alignment` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-038 are accepted.

Feature 038 passed all five exact-head checks at
`4305b0d06aa007741984a013ac354c95162cf5a0`, including the dedicated
thirteen-symbol ARM32 partial-libc errno integration.

Feature 039 is now integrated. It resolves current Android semantics against the
official `android-latest-release` baseline, recorded as `android17-release`
with Android 17.0.0 r1 exact-source comparisons.

The audit preserves the accepted namespace-link, atoi/strtol, guest errno, and
Android-log ABI boundaries. It also preserves the already accepted feature-033
memmem no-read fast-path ordering and adds its release-alignment regression.

The implementation change in this batch corrects `strncpy` source access:
the full destination count range is validated before mutation, but source bytes
are read only until NUL/count. Padding after an observed NUL no longer requires
fictitious source addressability. A sparse high-address regression places
`{'A',0}` at guest address `0xfffffffe` and requires four-byte padded output.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 038 at
`4305b0d06aa007741984a013ac354c95162cf5a0`:

- Linux A32 smoke `108523987365` — PASS.
- Android x86_64 address-space probe `108523987348` — PASS.
- Android arm64-v8a cross-build `108523987276` — PASS.
- ARM32 libc memory string shim integration `108523987139` — PASS.
- ARM32 liblog shim integration `108523987047` — PASS.

Feature 039 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Allocator, constructor/destructor lifecycle, ARM EABI memory helpers,
pthread/semaphore scheduling boundaries, libdl, libm, Android filesystem/search,
stable embedding API, and device execution remain later work.
