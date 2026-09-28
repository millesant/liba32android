# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: none; starting concrete APK/ZIP native-library acquisition.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, and ARM32 bionic
libdl load-policy semantics.

ARM32 libdl load policy is DONE at
`431904d7b0b0880691b503d76d69c24a18b90960`. The project operator confirmed
all required exact-head CI checks were green.

## Next post-roadmap follow-up

Move concrete Android native-library acquisition from extracted filesystem roots
into APK-style virtual paths such as
`base.apk!/lib/armeabi-v7a/libvlc.so`.

The supplied VLC 3.7.2 Beta 2 APK is about 67.3 MB and contains 2,617 ZIP
entries. Its four ARMv7 native libraries (`libvlcjni.so`, `libvlc.so`,
`libmla.so`, and `libc++_shared.so`) are all ZIP method 8 / DEFLATE, so the
archive source must support bounded DEFLATE rather than stored entries only.

Feature 048 already constructs requester-scoped APK-style virtual paths and the
accepted filesystem source proves the byte-source abstraction. The new source
should parse only the ZIP structures needed for exact entry lookup, enforce
finite archive/entry/central-directory/path ceilings, return owned bytes, and
reuse zlib for raw DEFLATE rather than introducing a general archive framework.

## Deferred / partial

Package-manager/AssetManager discovery, APK signature verification, installation
policy, split-APK selection, explicit-path dlopen, true lazy binding,
caller-relative RTLD_NEXT, process-exit Global/NODELETE teardown,
DF_1_GLOBAL-specific unload retention beyond explicit libdl root policy,
Retired-slot compaction, concurrent graph mutation, `DT_PREINIT_ARRAY`,
process argv/envp constructor ABI, broader pthread/TLS, higher-level public
ELF/platform orchestration, JNI/graphics/audio surfaces, and real Android device
execution remain separate.
