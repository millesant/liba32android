# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `041-a32-libc-allocator-shim` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-040 are accepted.

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

Feature 040 passed all five required exact-head checks at
`84d49653e6d72dca0e116ab2e12342efd4057531`.

Feature 041 is now integrated. It extends the real partial ARM32 `libc.so`
fixture and freestanding consumer with `malloc/calloc/realloc/free` at shared
SVC IDs `0xAE-0xB1`, requires seventeen eager JUMP_SLOT targets, and routes the
four real guest wrappers into one bounded `A32LibcGuestHeap` over a mapped
logical guest arena.

Feature 041 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Constructor/destructor lifecycle,
ARM EABI memory helpers, pthread/semaphore scheduling boundaries, libdl, libm,
Android filesystem/search, stable embedding API, and device execution remain
later work.
