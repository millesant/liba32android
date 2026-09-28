# Requester-scoped Android application library search

Status: feature 048 and filesystem source accepted; bounded APK source implemented, exact-head validation pending

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

The accepted filesystem integration established the same path using an extracted
child DSO. The current APK follow-up keeps the root image direct but packages
the generated child into a deterministic one-entry DEFLATED mini-APK with fixed
ZIP metadata.

The requester search root is the APK virtual directory
`fixture-app.apk!/lib/armeabi-v7a`. `A32ApkLibrarySource` reads and inflates
the child, after which the unchanged generic dependency loader forms the
two-object graph, applies the root's one JUMP_SLOT, and executes the root wrapper
through the archive-loaded child.

Focused host coverage independently exercises stored entries plus malformed,
resource, duplicate, ZIP64, encryption, compression, local-header, CRC, and
truncation failure cases.

## Limits

No AssetManager bridge, package-manager discovery, split-APK selection,
signature verification, extraction cache, ZIP64/encrypted archive support,
explicit slash-containing dlopen path, RUNPATH/RPATH, LD_LIBRARY_PATH,
namespace permitted-path enforcement, transitive namespace traversal, or
platform allowlist is introduced. Dynamic missing-object dlopen and unload
policy exist in their separate accepted ownership layers and are not changed by
this source.
