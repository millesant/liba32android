# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-a32-libc-cxa-service-aware-fini` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, bounded `__aeabi_atexit` registration,
registered-destructor finalization, and the guest `__cxa_finalize` service.

## Active post-roadmap follow-up

The partial ARM32 libc shim now exposes forty-one target-backed functions,
adding linked `__cxa_finalize` at SVC `0xD3`.

ELF lifecycle execution can optionally dispatch bounded host services while a
constructor/destructor runs. Service failures and suspension are explicit and
persistent lifecycle propagates the failing SVC identity while preserving
Failed-state no-replay semantics.

The real partial-libc consumer now has one controlled FINI_ARRAY entry that
calls linked `__cxa_finalize`. Integration registers a matching destructor,
executes FINI through the service-aware lifecycle seam, verifies the guest
destructor side effect and Complete state, then proves a repeated direct
finalize wrapper does not replay it.

Exact-head validation: NOT RUN.

## Deferred / partial

DSO-handle/link-map ownership, last-reference dlclose transaction ordering,
service-aware registered destructor callbacks, reference-counted mapping
reclamation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/TLS, dynamic missing-object libdl acquisition, concrete APK/ZIP byte
acquisition, higher-level public ELF/platform orchestration,
JNI/graphics/audio surfaces, and real Android device execution remain separate.
