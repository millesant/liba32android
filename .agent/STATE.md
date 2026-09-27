# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `036-a32-libc-integer-shim` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-035 are accepted.

Feature 035 passed all five exact-head checks at
`f32b11108d2f7f0efb8d2b0e449fe081a48c2606`.

Feature 036 is now integrated. It carries `atoi` and `strtol` through the
real partial ARM32 `libc.so`: twelve exact shim resolutions/JUMP_SLOT targets,
the separate bounded integer handler, real decimal conversion, and a logical
guest endptr write.

The accepted feature-033 memmem no-read correction remains preserved in the
integrated tree.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 035 at
`f32b11108d2f7f0efb8d2b0e449fe081a48c2606`:

- Linux A32 smoke `108515488580` — PASS.
- Android x86_64 address-space probe `108515488586` — PASS.
- Android arm64-v8a cross-build `108515488476` — PASS.
- ARM32 liblog shim integration `108515517327` — PASS.
- ARM32 libc memory string shim integration `108515488424` — PASS.

Feature 036 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work is prepared through feature 049:
`037 -> 038 -> 039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Guest errno/TLS, allocator, constructor/destructor lifecycle, ARM EABI memory
helpers, pthread/semaphore scheduling boundaries, libdl, libm, Android
filesystem/search, stable embedding API, and device execution remain later work.
