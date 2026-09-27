# Design — requester-scoped Android app-library search

## Input policy

The provider borrows:
- a finite ordered span of `{requester_identity, root}` records;
- one `A32AndroidLibrarySource`;
- one non-zero maximum candidate-path byte count.

Requester identity and requested name are matched byte-for-byte. This slice
accepts only bare dependency names: empty, NUL-containing, slash-containing, or
backslash-containing requests return NotFound without source I/O.

## Candidate construction

For every root matching the requester, append exactly one slash when needed and
then the requested name. Reject empty/NUL-containing roots and candidates beyond
the configured path ceiling.

No canonicalization, dot-segment handling, basename extraction, or path
permission inference is performed.

## Source result mapping

Call `source.load(candidate, max_image_bytes)` with the exact generic image
ceiling:
- NotFound -> try next matching root;
- Failed -> provider Failed;
- success -> require non-empty identity/image and image <= ceiling, then publish.

The source is responsible for concrete byte acquisition and may internally map
virtual paths to an APK, extracted directory, AssetManager, memory index, or
another store.

## Integration

Generate two freestanding ARM32 DSOs. The root contains one DT_NEEDED/JUMP_SLOT
reference to the child. Supply the root directly to the dependency loader and
make the child available only through a virtual
`base.apk!/lib/armeabi-v7a` source bound to the root's exact identity.

Successful loading, relocation, and A32 execution prove requester propagation,
candidate construction, source acquisition, graph ownership, symbol lookup, and
relocation composition end-to-end.
