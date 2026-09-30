# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-call-static-object-method` from validated raw
CallStaticVoidMethod revision `d660301b87f4ba9d91b56b129261255b860312b8`.

The same supplied VLC ARMv7 `libvlcjni.so` directly exposes raw
`CallStaticObjectMethod` at JNIEnv slot 114 / byte offset `0x1c8`.

This slice reuses the exact StaticMethod identity and raw r3-plus-stack AAPCS32
decoder. The bridge may return null or one pre-existing logical JNI reference;
a non-null result must receive a local reference before guest exposure.

Keep V/A variants, other static return types, Java class initialization and
dispatch, framework object creation, and broad Java heap modeling separate.

## Validation

Use exact-head commit checks after the focused host regression is committed.
Escalate only failing checks. After terminal success, leave CI and converge the
change state.
