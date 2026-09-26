# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `030-a32-libc-memory-string-service` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-029 are accepted.

Feature 029 passed exact-head Linux A32 smoke, both Android checks, and the real
ARM32 liblog integration at
`6df9ad2d63e1aceebbf30489a31c5c803b48bbbf`.

Feature 030 is now integrated. It adds the first bounded libc host-service
surface shared by the supplied FMOD and VLC ARMv7 binaries:
`memcpy/memset/memcmp/memchr/strlen/strcmp/strncmp`.

The service uses logical GuestMemory addresses, shared private SVC IDs
`0xA1-0xA7`, caller-selected transfer/string ceilings, source-before-
destination memcpy behavior, and portable comparison signs. It adds no guest
libc.so shim or stateful libc surface.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 029 at
`6df9ad2d63e1aceebbf30489a31c5c803b48bbbf`:

- Linux A32 smoke `108505154805` — PASS.
- Android x86_64 address-space probe `108505154737` — PASS.
- Android arm64-v8a cross-build `108505154640` — PASS.
- ARM32 liblog shim integration `108505154464` — PASS.

Feature 030 exact-head validation: NOT RUN.

## Deferred / partial

Guest libc shim/provider, broader libc state, Android filesystem/search,
pthread/TLS, libdl, I/O/stdio/socket, libm, JNI/graphics/audio, and Android
device end-to-end evidence remain later work.
