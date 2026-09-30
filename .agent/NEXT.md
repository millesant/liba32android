# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-static-method-id` from validated NewObjectV
revision `eb676dda5eac4682d3ce29c488df63ef0be3ed04`.

Supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
JNI_OnLoad directly loads JNIEnv byte offset `0x1c4`, slot 113
(`GetStaticMethodID`), then forwards JNIEnv, jclass, method-name, and
signature pointers in r0-r3.

Keep this slice bounded to exact slot 113, a distinct StaticMethod member kind,
and the existing bounded exact lookup/string-decoding contract. Do not fold in
CallStaticObjectMethod, CallStaticVoidMethod, Java dispatch, inheritance, or
framework behavior yet.

## Validation

Run focused member-ID host coverage first, then exact-head checks. After
terminal success, leave CI and converge evidence/state.
