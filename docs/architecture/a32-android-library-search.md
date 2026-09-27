# Requester-scoped Android application library search

Status: feature 048 accepted; post-roadmap filesystem source implemented, exact-head validation pending

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

The post-roadmap follow-up adds `A32FilesystemLibrarySource` for caller-owned
filesystem paths such as extracted/native-library directories.

The source consumes the candidate path exactly as supplied. It rejects empty,
embedded-NUL, over-ceiling paths, zero image ceilings, non-regular files,
empty files, and files exceeding the caller's exact `max_image_bytes` bound.
`ENOENT` and `ENOTDIR` map to `NotFound`; other open/stat/read failures map
to `Failed`. Reads retry interrupted syscalls and require the complete
size observed by `fstat`.

Successful identity is the exact path string used for the read. There is no
canonicalization, basename substitution, package-manager lookup, symlink policy,
or APK/ZIP interpretation in this source.

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

- `libfixture_app_root.so`, which needs
  `libfixture_app_child.so`;
- `libfixture_app_child.so`, which returns a known value.

The host integration supplies only the root image directly. The child is
acquired through `A32FilesystemLibrarySource` from the caller-provided fixture
directory. The generic dependency loader must propagate the root identity,
construct the exact candidate path, read the child under the image ceiling,
form a two-object graph, relocate the root's one JUMP_SLOT, and execute the root
wrapper to the child value.

The focused policy regression retains the original synthetic APK-style virtual
root case so archive-like path construction remains covered independently of
concrete filesystem I/O.

## Limits

No concrete APK/ZIP reader, AssetManager bridge, package-manager discovery,
explicit slash-containing dlopen path, RUNPATH/RPATH, LD_LIBRARY_PATH,
namespace permitted-path enforcement, transitive namespace traversal, platform
allowlist, dynamic missing-object dlopen transaction, or unload policy is
introduced.
