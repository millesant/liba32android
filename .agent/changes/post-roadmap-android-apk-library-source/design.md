# Design — bounded Android APK library source

Feature 048 already constructs requester-scoped candidates such as
`/path/base.apk!/lib/armeabi-v7a/libvlc.so`. Keep that search policy unchanged
and add one concrete source that interprets only the `!/` archive boundary.

The source opens the archive path before `!/` as a regular file and performs
exact central-directory lookup for the entry bytes after it. It is not an
extractor and never writes archive content to disk.

Use ordinary single-disk ZIP32 structures only. Locate EOCD within the ZIP
comment bound, reject ZIP64 sentinel fields and multi-disk metadata, and bound
entry count plus central-directory bytes before iterating. Each central entry
name is bounded before allocation. Duplicate exact names are ambiguous failure.

For the selected entry, reject encryption and unsupported compression. Validate
the local header signature, method, flags, and exact filename against the
central record before reading payload bytes. Stored entries copy directly.
DEFLATE entries use zlib with raw DEFLATE framing (`inflateInit2(-MAX_WBITS)`).
The central uncompressed size is bounded by the caller image ceiling and output
must end at the exact declared size. CRC32 is checked before publication.

Archive size has its own ceiling even though exact lookup reads only the EOCD,
central directory, local header, and selected compressed payload. This keeps
the concrete source finite even for hostile caller-provided archives.

The source result identity is the exact virtual path. Missing archive/path
components that map to ENOENT/ENOTDIR and a missing exact entry return
NotFound; structural/resource/codec failures return Failed.

The host/Android builds use system/NDK zlib rather than introducing a bundled
general ZIP framework. The real ARM32 integration creates a deterministic
mini-APK around the existing generated child DSO and proves the complete
requester-aware load/relocate/execute path through DEFLATE.
