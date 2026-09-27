# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-a32-registered-destructor-finalization` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 are accepted. The numbered roadmap remains COMPLETE.

Accepted post-roadmap slices include the Android filesystem source, persistent
legacy ELF lifecycle state, and bounded ARM EABI `__aeabi_atexit`
registration.

## Active post-roadmap follow-up

Registered `__aeabi_atexit` callbacks can now be finalized by exact DSO handle
or process-wide in reverse registration order.

Each registration tracks Pending, Complete, or Failed state. The finalizer
preflights a caller callback ceiling before any guest side effect, supplies the
registered object in r0, executes the registered ARM/Thumb destructor under a
finite instruction budget, suppresses Complete callbacks, and latches Failed on
invalid addresses or guest execution failure to prevent replay.

Focused regressions cover per-DSO reverse ordering, process-wide remaining
finalization, callback-ceiling no-side-effect behavior, once-only Complete
state, and terminal failure latching.

Exact-head validation: NOT RUN.

## Deferred / partial

Guest `__cxa_finalize`, DSO-handle/link-map ownership, dlclose/process-exit
ordering against FINI_ARRAY/DT_FINI, reference-counted unload and mapping
reclamation, `DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader
pthread/thread creation/TLS services, dynamic missing-object libdl acquisition,
concrete APK/ZIP byte acquisition, higher-level public ELF/platform
orchestration, JNI/graphics/audio surfaces, and real Android device execution
remain separate follow-up work.
