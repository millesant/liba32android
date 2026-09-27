# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `038-a32-libc-errno-shim` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-037 are accepted.

Feature 037 passed all five exact-head checks at
`d4390484a4df6435c74d6e165ecdef0f3a396b95`.

Feature 038 is now integrated. The reproducible partial ARM32 `libc.so`
exports `__errno` at SVC `0xAD`, the consumer dereferences the returned
logical guest int pointer, and real integration grows to thirteen exact
symbols/JUMP_SLOT targets.

The same `A32LibcGuestErrnoState` instance backs both strtol ERANGE
publication and `__errno`, so the real fixture proves the written guest errno
value is subsequently observed through the guest ABI.

The accepted feature-033 memmem no-read correction remains preserved.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 037 at
`d4390484a4df6435c74d6e165ecdef0f3a396b95`:

- Linux A32 smoke `108520332704` — PASS.
- Android arm64-v8a cross-build `108520332702` — PASS.
- Android x86_64 address-space probe `108520332573` — PASS.
- ARM32 libc memory string shim integration `108520332518` — PASS.
- ARM32 liblog shim integration `108520332427` — PASS.

Feature 038 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`039 -> 040 -> 041 -> 042 -> 043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Android TLS/thread selection, allocator, constructor/destructor lifecycle, ARM
EABI memory helpers, pthread/semaphore scheduling boundaries, libdl, libm,
Android filesystem/search, stable embedding API, and device execution remain
later work.
