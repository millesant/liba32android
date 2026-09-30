# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-instance-int-field` from validated raw
CallVoidMethod revision `1383d7cd44b3b0a669e9e2a3e6fd7915d747efcc`.

The supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
directly proves `GetIntField` at JNIEnv slot 100 / byte offset `0x190` in
`Java_org_videolan_libvlc_Media_nativeNewFromFd`. The call forwards JNIEnv,
jobject, and cached jfieldID in r0-r2 and consumes the jint result from r0.

Keep this slice bounded to:

- publish only evidence-backed GetIntField slot 100;
- store caller-seeded signed 32-bit values by logical jobject + existing
  InstanceField ID;
- require live receiver/member/value state and return exact jint bits in r0;
- reuse the existing deterministic logical-reference/member model;
- keep SetIntField, Java field layout/class assignability, inheritance,
  volatile semantics, and the adjacent ExceptionOccurred/ExceptionClear calls
  separate.

## Validation

Implement focused host coverage before exact-head checks. After terminal CI
success, leave CI immediately and converge the accepted change state.
