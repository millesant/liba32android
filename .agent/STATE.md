# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `035-a32-libc-integer-service` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-034 are accepted.

Feature 034 passed all five exact-head checks at
`e4215d8c974ca5c4a6a49bad82918597d56d61e1`, including the dedicated real
ARM32 ten-symbol partial-libc fixture/integration.

Feature 035 is now integrated. It adds bounded ARM32 Android `atoi` and
`strtol` host services at private SVC IDs `0xAB/0xAC`, with signed
32-bit result semantics, bounded base/prefix parsing, endptr publication, and
caller-owned guest errno publication for EINVAL/ERANGE.

The accepted feature-033 memmem no-read correction remains preserved in the
integrated tree.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 034 at
`e4215d8c974ca5c4a6a49bad82918597d56d61e1`:

- Linux A32 smoke `108513006508` — PASS.
- Android x86_64 address-space probe `108513006502` — PASS.
- Android arm64-v8a cross-build `108513006419` — PASS.
- ARM32 liblog shim integration `108513005936` — PASS.
- ARM32 libc memory string shim integration `108513005915` — PASS.

Feature 035 exact-head validation: NOT RUN.

## Deferred / partial

The matching atoi/strtol guest-shim exports, guest errno slot, allocator,
constructor/destructor lifecycle, ARM EABI memory helpers, pthread/TLS,
I/O/stdio/socket, libdl, libm, Android filesystem/search, stable embedding API,
and device execution remain later work.
