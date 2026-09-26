# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `034-a32-libc-copy-search-shim` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-033 are accepted.

Feature 033's first exact-head attempt exposed a real memmem no-read ordering
bug. The corrective revision
`f13febc07bfdd7564951979f9b988e9a14cbd025` moved empty-needle and
short-haystack result paths ahead of unused guest-range validation; all five
required checks then passed.

Feature 034 is now integrated. It extends the reproducible partial ARM32
`libc.so` and consumer from seven to ten functions by adding
`memmem/strcpy/strncpy`, with ten exact shim resolutions, eager JUMP_SLOT
targets, exact-SVC registry entries, and real wrapper execution.

The feature-033 memmem correction is preserved in the integrated feature-034
tree rather than reintroducing the older prepared implementation.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 033 at
`f13febc07bfdd7564951979f9b988e9a14cbd025`:

- Linux A32 smoke `108511986967` — PASS.
- Android arm64-v8a `108511986963` — PASS.
- ARM32 liblog shim integration `108511986937` — PASS.
- Android x86_64 `108511986837` — PASS.
- ARM32 libc memory string shim integration `108511986625` — PASS.

Feature 034 exact-head validation: NOT RUN.

## Deferred / partial

Integer/errno, allocator, lifecycle registration/finalization, ARM EABI memory
helpers, pthread/TLS, I/O/stdio/socket, libdl, libm, Android filesystem/search,
stable embedding API, and device execution remain later work.
