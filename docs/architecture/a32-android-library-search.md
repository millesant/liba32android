# Requester-scoped Android application library search

Status: feature 048 implemented; exact-head validation pending

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

A concrete embedding may back this with filesystem reads, an APK/ZIP reader,
AssetManager, a pre-indexed archive, or another store. Feature 048 itself does
none of that I/O.

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

This makes both examples below representable without teaching the ELF resolver
about Android:

- `base.apk!/lib/armeabi-v7a/libvlc.so`
- `/data/app/example/lib/arm/libvlc.so`

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

The host integration supplies only the root image directly. The child image is
available solely through a virtual
`base.apk!/lib/armeabi-v7a` source. The generic dependency loader must ask
with the root object's exact identity, receive the searched child, form the
two-object graph, relocate the root's one JUMP_SLOT, and execute the root
wrapper to the child value.

## Limits

No concrete filesystem or ZIP reader, explicit slash-containing dlopen path,
RUNPATH/RPATH, LD_LIBRARY_PATH, namespace permitted-path enforcement,
transitive namespace traversal, package-manager discovery, platform allowlist,
dynamic missing-object dlopen transaction, or unload policy is introduced.
