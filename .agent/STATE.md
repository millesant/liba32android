# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `040-a32-libc-guest-heap` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-039 are accepted.

Feature 039 passed all five exact-head checks at
`9160aa24c9f1cebdfe65a79ecc1543a662bec5c4`.

Feature 040 is now integrated. It adds bounded ARM32 `malloc/calloc/realloc/free`
host services at private SVC IDs `0xAE-0xB1` over a caller-provided,
already-mapped writable guest arena and finite caller-owned allocation metadata.

The heap returns only logical 32-bit guest addresses, uses deterministic
16-byte-aligned first-fit placement, publishes Android ENOMEM through the
feature-037 guest errno sink, preserves old allocations on failed realloc
growth, and owns no guest mappings.

The accepted feature-039 Android-17 alignment and feature-033 memmem correction
remain preserved.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 039 at
`9160aa24c9f1cebdfe65a79ecc1543a662bec5c4`:

- Linux A32 smoke `108524978723` — PASS.
- Android arm64-v8a cross-build `108524978738` — PASS.
- Android x86_64 address-space probe `108524978623` — PASS.
- ARM32 liblog shim integration `108524978481` — PASS.
- ARM32 libc memory string shim integration `108524978431` — PASS.

Feature 040 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

The matching allocator guest-shim exports, constructor/destructor lifecycle,
ARM EABI memory helpers, pthread/semaphore scheduling boundaries, libdl, libm,
Android filesystem/search, stable embedding API, and device execution remain
later work.
