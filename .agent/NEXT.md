# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-call-static-void-method` from validated
GetStaticMethodID revision `8e8280b9a5479cb2629e6bd8b83bf0371aa910cb`.

The supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
directly calls JNIEnv byte offset `0x234`, slot 141
(`CallStaticVoidMethod`), in
`Java_org_videolan_libvlc_Dialog_QuestionDialog_nativePostAction`.
The call places JNIEnv, jclass, and cached static jmethodID in r0-r2, the first
variadic Java argument in r3, and remaining words on the guest stack.

Keep this slice bounded to raw CallStaticVoidMethod, live exact jclass +
StaticMethod validation, the accepted raw descriptor/AAPCS32 argument decoder,
and a caller-owned synchronous static void-call bridge. Leave
CallStaticVoidMethodV/A, CallStaticObjectMethod, return-valued static calls,
Java dispatch/class initialization, and framework behavior separate.

## Validation

Add focused raw static-call host coverage first, then use exact-head checks.
After terminal success, leave CI and converge evidence/state.
