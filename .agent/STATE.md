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

Exact-head validation: FAILED at `07ab332fc0566da3218cc3dcee6c66d12b143ded`.
Linux A32 smoke failed to compile the new lifecycle regression because its
`Elf32FiniExecutionOptions` test alias was missing. The ARM32 partial-libc
integration reached its controlled FINI_ARRAY assertion but did not complete
the expected registered-finalizer path. The correction restores the alias and
moves static-destructor registration into the ARM32 consumer, so the registered
destructor/DSO words and later `__cxa_finalize(&fixture_dso_handle)` use the
same guest relocation path. A bounded diagnostic is emitted only if the FINI
assertion fails again.

Corrected exact-head validation at `85b7e9f3211eff47288232eee8b30517fe380a01`: FAILED only in ARM32 libc integration; the other eight required checks passed.

The bounded diagnostic proved the registered finalizer itself succeeded:
marker `0xC0DEC0DE` was written, the record became Complete, and
`callbacks_completed=1`. The outer FINI call then reported MemoryFault before
returning. Root cause: the guest `__cxa_finalize` service started its nested
registered destructor at the configured lifecycle stack top, overwriting the
still-active FINI caller frame at -O0. The service now overrides callback
`stack_top` with the trapped guest's live r13 so nested destructors grow below
the active frame.

Second corrected exact-head validation: NOT RUN.

## Deferred / partial

DSO-handle/link-map ownership, last-reference dlclose transaction ordering,
service-aware registered destructor callbacks, reference-counted mapping
reclamation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/TLS, dynamic missing-object libdl acquisition, concrete APK/ZIP byte
acquisition, higher-level public ELF/platform orchestration,
JNI/graphics/audio surfaces, and real Android device execution remain separate.
