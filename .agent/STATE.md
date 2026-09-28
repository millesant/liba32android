# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-android-apk-native-catalog` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, ARM32 bionic
libdl policy, bounded APK native-library acquisition, and caller-supplied APK
runtime bootstrap composition.

Caller-supplied APK runtime bootstrap is DONE at
`268d151bb8c93473c27abdefb9b9644b51ff3c96`. The project operator confirmed
all required exact-head CI checks were green after correcting the child
synthetic-handle expectation to the accepted four-byte handle stride.

## Active post-roadmap follow-up

Bounded APK native-library catalog discovery is implemented.

`A32ApkLibrarySource::catalog` reuses the exact same private ZIP32 archive-open
and central-directory parser as exact-entry `load`. The refactor returns
bounded owned central metadata once; exact loading selects one record while the
catalog filters the same validated records.

For one caller-selected relative ABI directory, only direct child basenames
ending exactly in `.so` qualify. Nested paths, other ABI directories, and
non-native entries are ignored. Qualifying names must be bare/NUL-free and are
published as owned lexicographically sorted SONAME strings.

Caller-selected catalog limits bound discovered library count, per-SONAME bytes,
total SONAME bytes, and ABI-directory bytes. Existing APK source limits continue
to bound archive size, ZIP entry count, central-directory bytes, ZIP entry-name
bytes, and path bytes. Duplicate direct SONAMEs fail as ambiguous.

Focused host regressions cover filtering/order, duplicate ambiguity, missing APK,
unsafe ABI directories, catalog count/name/total-name bounds, inherited
archive/entry/central/name ZIP bounds, ZIP64 rejection, and feeding discovered
strings directly into `A32AndroidApkRuntimeBootstrap`.

The real pinned-NDK ARM32 bootstrap integration now catalogs its deterministic
two-DSO DEFLATED APK before constructing the bootstrap. Only the initial root
SONAME remains explicit. The workflow asserts `catalog_count=2` before the
existing root/child load, relocation, execution, and persistent child-dlopen
evidence.

The supplied VLC APK direct `lib/armeabi-v7a/` catalog is exactly:
`libc++_shared.so`, `libmla.so`, `libvlc.so`, and `libvlcjni.so`.

Exact-head validation: NOT RUN.

## Deferred / partial

JNI has no implementation surface yet: no JNIEnv, JavaVM, JNI_OnLoad, or
RegisterNatives support is present.

ABI auto-detection, manifest/root-library auto-selection, split-APK merging,
package-manager/AssetManager discovery, APK signatures, ZIP64/encrypted archive
support, explicit-path dlopen, true lazy binding, caller-relative RTLD_NEXT,
process-exit Global/NODELETE teardown, DF_1_GLOBAL retention outside explicit
libdl policy, Retired-slot compaction, concurrent graph mutation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
higher-level public platform orchestration, graphics/audio surfaces, automatic
app patching, and real Android device execution remain separate.
