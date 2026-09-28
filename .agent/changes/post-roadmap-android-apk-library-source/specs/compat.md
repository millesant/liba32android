# Compatibility spec delta — bounded APK native-library source

`A32ApkLibrarySource` implements the existing synchronous
`A32AndroidLibrarySource` contract for exact virtual paths of the form
`<archive>!/<entry>`.

The source accepts only non-empty, NUL-free virtual paths under a caller path
ceiling. It opens the archive portion as one regular file, enforces a caller
archive byte ceiling, locates an ordinary single-disk ZIP32 EOCD within the
standard comment window, and rejects ZIP64 sentinels or multi-disk metadata.

Central-directory entry count and bytes are independently bounded. Every entry
name is bounded before allocation. The source scans exact byte names only;
missing archive/entry returns NotFound and duplicate exact names are Failed.

The selected central record must describe a non-empty stored or DEFLATE entry
under the caller image ceiling. Encryption and unsupported compression fail.
The local header must agree on exact name, flags, and method. When the data-
descriptor flag is absent, local CRC/sizes must also agree with the central
record. Payload range must remain before the central directory.

Stored bytes are returned directly. DEFLATE uses platform/NDK zlib raw framing.
Inflation must consume the entire declared compressed payload and produce
exactly the declared uncompressed size. CRC32 over the final image must match
the central record before publication.

Successful source identity is the exact input virtual path and image storage is
owned. No archive content is extracted to disk.

The requester-scoped Android search provider is unchanged and can therefore
compose an APK root such as
`/path/base.apk!/lib/armeabi-v7a` with a bare DT_NEEDED name.

Package-manager/AssetManager discovery, split-APK selection, signatures,
installation policy, extraction caches, ZIP64, encrypted archives, and general
archive rewriting remain separate.
