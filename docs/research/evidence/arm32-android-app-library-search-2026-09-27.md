# ARM32 Android app-library search evidence — 2026-09-27

## Supplied target layout

The supplied VLC Android APK contains these ARMv7 native libraries under the
standard package-native directory:

- `lib/armeabi-v7a/libc++_shared.so`
- `lib/armeabi-v7a/libmla.so`
- `lib/armeabi-v7a/libvlc.so`
- `lib/armeabi-v7a/libvlcjni.so`

Bounded dynamic-section inspection shows app-local dependency edges that cannot
be satisfied by platform compatibility libraries alone:

- `libvlcjni.so` needs `libvlc.so`;
- `libmla.so` needs `libvlc.so` and `libc++_shared.so`.

The supplied FMOD image, by contrast, needs only the inspected platform-style
set `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so`, and `libdl.so`.

This establishes a concrete requirement for requester-aware application-native
library acquisition in addition to the already accepted platform compatibility
catalog.

## Existing project boundary

The generic ELF dependency resolver already accepts a requester identity and an
injected provider while explicitly keeping filesystem/search behavior outside
the generic ELF layer. Feature 029 models only linked-namespace SONAME
accessibility and intentionally defers search/permitted paths and APK lookup.

Feature 048 therefore adds a compatibility-layer search policy without putting
Android path construction into the generic loader.

## Search model selected for feature 048

The target-backed minimum is ordered bare-SONAME search inside caller-supplied
requester roots. A virtual root such as
`base.apk!/lib/armeabi-v7a` can represent package-native storage, while a
root such as `/data/app/.../lib/arm` can represent an extracted/native
filesystem directory.

Concrete byte acquisition is deliberately abstracted behind
`A32AndroidLibrarySource`. That keeps ZIP parsing, AssetManager APIs, host
filesystem ownership, permissions, and packaging-specific I/O outside this
bounded policy slice while preserving the exact candidate path and resource
ceiling.

## Android baseline and limits

The accepted Android 17 alignment records that actual bionic linker behavior is
richer than feature 029: loaded-library checks, library loading, linked
namespaces, configured search/permitted paths, explicit paths, and other linker
policy participate in the real system.

Feature 048 does not claim to reproduce that entire linker. It specifically
covers the app-local bare-SONAME dependency shape demonstrated by the supplied
VLC package, with deterministic caller-provided root ordering.
