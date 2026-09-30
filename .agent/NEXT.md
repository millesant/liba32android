# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-byte-array-elements` from validated
CallVoidMethodV revision `8a528b9402a874e8d1520687dc5920248234af7b`.

The supplied ARMv7 `libfmod.so`
(`sha256:982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`)
provides the next direct JNI seam. Its exported
`Java_org_fmod_MediaCodec_fmodReadAt` loads JNIEnv native-table offsets
`0x2e0` and `0x300`, i.e. slots 184 and 192:
`GetByteArrayElements` and `ReleaseByteArrayElements`.

Keep this slice bounded to:

- caller-seeded logical `jbyteArray` storage with generic GetArrayLength metadata;
- exact slot 184/192 publication only;
- one bounded guest scratch-copy lease with `isCopy = JNI_TRUE`;
- Release mode 0 / JNI_COMMIT / JNI_ABORT copy-back semantics;
- live logical-reference validation and exact leased array/pointer identity;
- no NewByteArray, byte-region APIs, other primitive arrays, Java framework behavior,
  or host-pointer publication.

## Validation

Implement focused host coverage first, then use exact-head checks once. Do not
spend the work round polling CI after terminal success or while useful local
engineering remains.
