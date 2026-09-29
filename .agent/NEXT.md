# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-long-arrays`.

The modified-UTF-8 string slice is complete at
`d06d2ec0393c0cb12fb414d07d618b5bf1c7f07d` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives one coherent four-slot jlong family:

1. NewLongArray at JNIEnv slot 180 / offset `0x2d0`;
2. GetLongArrayElements at slot 188 / `0x2f0`;
3. ReleaseLongArrayElements at slot 196 / `0x310`;
4. SetLongArrayRegion at slot 212 / `0x350`;
5. synthetic collision-free array handles, zero-initialized owned int64 storage,
   generic GetArrayLength metadata, and one local reference;
6. one 8-byte-aligned guest element scratch lease with mode 0/JNI_COMMIT/
   JNI_ABORT copy-back semantics;
7. exact ARM32 fifth-argument decoding for SetLongArrayRegion through guest
   `[sp]`;
8. pinned-NDK ARM32 fixture coverage of all four calls.

Do not generalize other primitive types, object arrays, critical-array APIs,
pinning, or concurrent element leases until the next evidence-backed slice.
