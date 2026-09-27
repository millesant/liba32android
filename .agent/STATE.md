# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, bounded ARM EABI `__aeabi_atexit` registration,
and registered-destructor finalization.

## Post-roadmap registered-finalizer validation

Registered finalization passed all nine exact-head checks at
`1640ddc5e0958acbaeff4596f71338de65aa6081`.

Per-DSO and process-wide selection execute matching pending callbacks in reverse
registration order under callback/instruction ceilings. Successful callbacks
latch Complete; invalid/failed callbacks latch Failed and are not replayed.

## Deferred / partial

Guest `__cxa_finalize`, DSO-handle/link-map ownership, dlclose/process-exit
ordering against FINI_ARRAY/DT_FINI, reference-counted unload and mapping
reclamation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/thread creation/TLS services, dynamic missing-object libdl acquisition,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate follow-up work.
