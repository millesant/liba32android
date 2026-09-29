# Compatibility spec delta — ARM32 JNI jlong arrays

The JNIEnv table publishes:

- NewLongArray at slot 180 / byte offset `0x2d0`;
- GetLongArrayElements at slot 188 / `0x2f0`;
- ReleaseLongArrayElements at slot 196 / `0x310`;
- SetLongArrayRegion at slot 212 / `0x350`.

Each targets a distinct private ARM service stub.

The bounded registry creates zero-initialized logical jlong arrays with owned
signed 64-bit element storage, synthetic collision-free logical handles, generic
array-length metadata, and one initial local reference. Length is a signed jsize
and must be nonnegative and within the configured hard-capped element limit.

GetLongArrayElements supports one outstanding copy lease, copies all elements
to one caller-owned 8-byte-aligned guest scratch region, writes JNI_TRUE through
non-null isCopy, and returns that logical scratch address.

ReleaseLongArrayElements requires the exact leased array and scratch pointer.
Mode 0 copies back and releases, JNI_COMMIT copies back and retains the lease,
and JNI_ABORT releases without copying back.

SetLongArrayRegion validates signed start/length bounds and reads its fifth
ARM32 argument from guest `[sp]` as the source jlong pointer. Values are decoded
as little-endian 64-bit quantities.

Other primitive types, object arrays, critical-array APIs, pinning, and multiple
simultaneous element leases remain outside this slice.
