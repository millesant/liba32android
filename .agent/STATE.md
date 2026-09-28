# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-android-apk-library-source` — IMPLEMENTED, exact-head validation NOT RUN.

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

## Active post-roadmap follow-up

Concrete APK native-library acquisition is implemented.

`A32ApkLibrarySource` consumes exact `<archive>!/<entry>` virtual paths
without extraction. Caller-selected ceilings bound virtual path, archive size,
ZIP entry count, central-directory bytes, entry-name bytes, and published image
bytes.

The source implements a deliberately small single-disk ZIP32 subset. It rejects
ZIP64 sentinels, encryption, duplicate exact names, unsupported compression,
malformed/truncated records, selected local/central metadata disagreement,
resource-limit violations, decompression failures, and CRC mismatch.

Stored entries copy directly. ZIP method 8 uses platform/NDK zlib raw DEFLATE;
the full compressed stream and exact declared output size must match before
CRC32-validated owned bytes are published.

The supplied VLC 3.7.2 Beta 2 APK evidence is 67,261,250 bytes with 2,617 ZIP32
entries and a 228,892-byte central directory. All four
`lib/armeabi-v7a/*.so` entries use DEFLATE, proving this codec is needed for
the real target.

Focused host regressions cover stored/DEFLATE success, requester-search
composition, missing archive/entry, malformed virtual paths, archive/entry/
central/name/image ceilings, encryption, unsupported compression, CRC failure,
local-header disagreement, duplicate names, ZIP64 sentinel rejection, and
truncation.

The real pinned-NDK ARM32 app-search fixture now packages its child DSO into a
deterministic one-entry DEFLATED mini-APK. The integration path is requester
search -> APK source -> ELF dependency load -> JUMP_SLOT relocation -> ARM32
execution.

Host builds use CMake ZLIB; Android cross-builds link the NDK/system `libz`.
Standalone Linux workflows install `zlib1g-dev` explicitly.

Exact-head validation: NOT RUN.

## Deferred / partial

Caller-supplied APK/runtime bootstrap composition, package-manager/AssetManager
discovery, split-APK selection, APK signature verification, installation policy,
extraction caches, ZIP64/encrypted archive support, explicit-path dlopen, true
lazy binding, caller-relative RTLD_NEXT, process-exit Global/NODELETE teardown,
DF_1_GLOBAL-specific unload retention beyond explicit libdl root policy,
Retired-slot compaction, concurrent graph mutation, `DT_PREINIT_ARRAY`,
process argv/envp constructor ABI, broader pthread/TLS, higher-level public
ELF/platform orchestration, JNI/graphics/audio surfaces, and real Android device
execution remain separate.
