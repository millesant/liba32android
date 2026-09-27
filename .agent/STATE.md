# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `048-a32-android-library-search-provider` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-047 are accepted.

## Validation and repository-truth evidence

Feature 046 passed all six required exact-head checks at
`6696d15513e8a7d7867ec2f4dd19adfa961e3450`.

Feature 047 passed all seven required exact-head checks at
`8790fec8894159e034a60c5b86e5552dd375d39b`.

Feature 048 is now integrated. It adds finite requester-scoped Android
application native-library search over caller-owned virtual roots and a
caller-owned byte source. Bare SONAME requests can fall through ordered
APK/filesystem-style roots while concrete I/O remains outside the provider.
A real ARM32 root/child fixture proves requester propagation, APK-style virtual
path construction, dependency loading, eager JUMP_SLOT relocation, and child
execution.

Feature 048 exact-head validation: NOT RUN.

Prepared order remains:
`049`.

## Deferred / partial

Legacy DT_INIT/DT_FINI, __aeabi_atexit/static-destructor registration, and
persistent lifecycle state, broader pthread/thread creation/TLS services,
dynamic libdl acquisition/unload semantics, broader/exceptional libm semantics,
concrete APK/filesystem byte acquisition and richer Android search policy,
stable embedding API, and device execution remain later work.
