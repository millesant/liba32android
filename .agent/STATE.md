# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `049-stable-c-embedding-api` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-048 are accepted.

## Validation and repository-truth evidence

Feature 047 passed all seven required exact-head checks at
`8790fec8894159e034a60c5b86e5552dd375d39b`.

Feature 048 passed all eight required exact-head checks at
`765429cb1db3a0e1e5f3b49b75e1c0f0712e9808`.

Feature 049 is now integrated. It adds public C embedding API version 1 with
opaque runtime lifetime, logical guest memory map/protect/unmap/read/write,
bounded ARM/Thumb execution, exact successful SVC traps, stable status/error
delivery, build/install public headers, and a dedicated staged-install external
C consumer workflow.

Feature 049 exact-head validation: NOT RUN.

The numbered 011-049 implementation roadmap is fully integrated; feature 049 is
the final numbered acceptance gate.

## Deferred / partial after numbered roadmap

Legacy DT_INIT/DT_FINI, __aeabi_atexit/static-destructor registration and
persistent lifecycle state, broader pthread/thread creation/TLS services,
dynamic libdl acquisition/unload semantics, broader/exceptional libm semantics,
concrete APK/filesystem byte acquisition and richer Android search policy,
higher-level public ELF/platform orchestration, and device execution remain
separate follow-up work.
