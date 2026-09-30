# Compatibility spec delta — ARM32 JNI byte-array element leases

- Publish `GetByteArrayElements` at JNIEnv slot 184 / byte offset `0x2e0`.
- Publish `ReleaseByteArrayElements` at slot 192 / byte offset `0x300`.
- Each entry uses a distinct private ARM `svc; bx lr` stub.
- Caller-seeded `jbyteArray` state owns bounded bytes behind a logical handle.
- Generic array-length metadata and existing local/global reference counts
  remain authoritative for length and liveness.
- `GetByteArrayElements` returns one bounded guest scratch copy at a time and
  writes JNI_TRUE to a non-null `isCopy` output.
- `ReleaseByteArrayElements` requires exact leased array/pointer identity.
  Mode 0 copies back and releases, JNI_COMMIT copies back and retains the
  lease, and JNI_ABORT releases without copy-back.
- Unknown/dead handles, overlapping leases, insufficient scratch capacity,
  invalid release modes, wrong pointers, and guest-memory failures are rejected.

`NewByteArray`, byte-region APIs, other primitive-array families, Java
framework behavior, and host-pointer publication remain outside this delta.
