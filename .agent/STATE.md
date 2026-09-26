# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `032-a32-libc-memory-string-shim-provider` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-031 are accepted.

Feature 031 passed exact-head Linux A32 smoke, both Android checks, and the
existing ARM32 liblog integration at
`37a14a897bc9867815bdabc92ef2dc65426e0991`.

Feature 032 is now integrated. It adds a reproducible freestanding ARMv7
partial `libc.so` exporting only
`memcpy/memset/memcmp/memchr/strlen/strcmp/strncmp`, plus a freestanding
consumer that imports all seven through ordinary dynamic relocations.

The real integration loads the partial libc through the feature-031 finite
platform catalog under feature-029 namespace gating, requires a JUMP_SLOT
target for every shim function, and executes all seven real wrappers through
the feature-030 bounded host service before returning to the requested stop PC.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 031 at
`37a14a897bc9867815bdabc92ef2dc65426e0991`:

- Linux A32 smoke `108507627185` — PASS.
- Android x86_64 address-space probe `108507627188` — PASS.
- Android arm64-v8a cross-build `108507627109` — PASS.
- ARM32 liblog shim integration `108507626811` — PASS.

Feature 032 exact-head validation: NOT RUN. Its dedicated partial-libc fixture
check is part of the acceptance gate.

## Deferred / partial

Allocator, integer/errno extensions, pthread/TLS, I/O/stdio/socket, libdl,
libm, Android filesystem/search, stable embedding API, broader relocation,
unload lifecycle, and Android-device evidence remain later work.
