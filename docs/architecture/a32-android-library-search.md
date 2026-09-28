# Requester-scoped Android application library search

Status: feature 048, filesystem source, and bounded APK source accepted; caller-supplied APK runtime bootstrap implemented

## Goal

Add the smallest Android-facing acquisition policy needed to load application
native dependencies such as the supplied VLC package's
`libvlcjni.so -> libvlc.so` and
`libmla.so -> {libvlc.so, libc++_shared.so}` edges.

The generic ELF layer remains unaware of Android paths and APK layout.

## Source boundary

`A32AndroidLibrarySource` is an abstract synchronous byte source:

`load(virtual_path, max_image_bytes)`

It returns exactly one of success, not found, or hard failure. On success it
provides an opaque loaded-object identity plus owned ELF image bytes.

Feature 048 proved that this abstraction can represent APK-style virtual roots
without putting archive/search semantics into the ELF loader.

## Concrete filesystem source

The accepted post-roadmap follow-up adds `A32FilesystemLibrarySource` for
caller-owned filesystem paths such as extracted/native-library directories.

The source consumes the candidate path exactly as supplied. It rejects empty,
embedded-NUL, over-ceiling paths, zero image ceilings, non-regular files,
empty files, and files exceeding the caller's exact `max_image_bytes` bound.
`ENOENT` and `ENOTDIR` map to `NotFound`; other open/stat/read failures map
to `Failed`. Reads retry interrupted syscalls and require the complete size
observed by `fstat`.

Successful identity is the exact path string used for the read. There is no
canonicalization, basename substitution, package-manager lookup, symlink policy,
or APK/ZIP interpretation in this source.

## Concrete APK source

The current follow-up adds `A32ApkLibrarySource` for exact virtual paths of
the form `<archive>!/<entry>`.

The implementation opens only the archive file and never extracts content to
disk. Caller options bound the complete virtual path, archive bytes, ZIP entry
count, central-directory bytes, and entry-name bytes; the existing source call
also supplies the exact output-image ceiling.

The parser deliberately implements a small ordinary ZIP32 subset. It locates
single-disk EOCD within the standard comment window, scans bounded central
records by exact byte name, validates the selected local header, and rejects
ZIP64 sentinels, encryption, unsupported compression, duplicates, malformed
records, truncation, or integrity mismatches.

Stored entries are copied directly. Method-8 entries are inflated with
platform/NDK zlib using raw DEFLATE framing. The full compressed stream must be
consumed, output must equal the declared uncompressed size, and CRC32 must match
before the source publishes owned bytes.

The supplied VLC 3.7.2 Beta 2 APK provides real target evidence: it is an
ordinary ~67.3 MB ZIP32 archive with 2,617 entries, and each of its four
`lib/armeabi-v7a/*.so` entries uses DEFLATE. That makes DEFLATE support a
production requirement rather than speculative archive breadth.

## Search roots

`A32AndroidLibrarySearchProvider` borrows a finite ordered set of records:

`{ requester_identity, root }`

Only exact requester identity matches are considered. Requested dependencies
must be bare names. Empty names, embedded NUL, forward slashes, and backslashes
are not searched by this provider.

For each matching root, the provider constructs one bounded virtual path and
calls the source. NotFound continues to the next root; Failed stops; first
validated success wins. The generic per-image byte ceiling is forwarded
unchanged and also checked before publication.

This makes both examples representable without teaching the ELF resolver about
Android:

- `base.apk!/lib/armeabi-v7a/libvlc.so` through an archive-aware source;
- `/data/app/example/lib/arm/libvlc.so` through the filesystem source.

## Persistent APK runtime bootstrap

`A32AndroidApkRuntimeBootstrap` moves one layer above byte acquisition without
creating a new loader. The caller supplies the APK path, exact ABI directory,
finite complete app-local SONAME set, platform provider, persistent runtime
state, guest stack, and existing resource ceilings.

For every declared SONAME the bootstrap owns an exact virtual identity under the
APK ABI root. It then composes three provider layers:

1. the accepted requester-scoped application search provider;
2. an APK-local guard/root provider; and
3. the caller's existing platform provider.

This ordering preserves exact requester semantics. Context-free initial or later
app-local opens pass through inert requester search and are acquired by the
guard/root provider. Transitive app dependencies are served by requester search
first. A declared app-local name that is absent from the APK fails at the guard
rather than falling through to a same-named platform object; undeclared
platform names still fall through normally.

The bootstrap owns one `A32LibDlOpenTransaction` over that persistent provider
chain. `open_root` only validates declared-root membership and then delegates
to the transaction, so dependency append, relocation, RELRO, constructors,
handle publication, policy promotion, and failure cleanup remain the accepted
implementations.

The bootstrap must stay alive while a libdl service or later named dlopen uses
its provider/open-transaction state.

## Composition

The provider is intended to sit before the existing platform compatibility
provider in `Elf32DependencyProviderChain`:

application-native search -> platform compatibility catalog

An app-local SONAME can therefore resolve from a requester root first, while a
platform SONAME can fall through to the feature-031 namespace-gated catalog.

## Real ARM32 integration

The pinned-NDK fixture builds:

- `libfixture_app_root.so`, which needs `libfixture_app_child.so`;
- `libfixture_app_child.so`, which returns a known value.

The accepted filesystem integration first proved requester-aware loading with an
extracted child, and the accepted APK-source follow-up then moved the child into
a deterministic DEFLATED mini-APK.

The current bootstrap integration packages both `libfixture_app_root.so` and
`libfixture_app_child.so` into the same deterministic DEFLATED APK. The
runtime starts with an empty link map, declares those two app-local SONAMEs,
bootstraps the root through `A32AndroidApkRuntimeBootstrap`, resolves the child
transitively through exact requester search, lets the existing open transaction
relocate and initialize both objects, and executes the root wrapper to the
child's known value. It then opens the already-resident child through the same
persistent open transaction and receives a second synthetic handle.

Focused host coverage separately proves provider ordering, exact identities,
declared-name fail-closed behavior, platform fallback, configuration bounds,
stored/DEFLATE source behavior, and malformed/resource archive failures.

## Limits

No AssetManager bridge, package-manager/manifest discovery, ABI auto-selection,
split-APK selection, signature verification, extraction cache, ZIP64/encrypted
archive support, explicit slash-containing dlopen path, RUNPATH/RPATH,
LD_LIBRARY_PATH, automatic patching, JNI startup, graphics/audio integration,
or device deployment is introduced. The bootstrap consumes caller-supplied
application membership and platform policy rather than inferring either.
