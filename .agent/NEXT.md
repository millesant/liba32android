# NEXT

## Active JNI track

Continue from validated raw CallStaticVoidMethod revision `9369f8d10d6f825c9ba6e58c2d8f2e892953d6d0`.

Direct Thumb-2 inspection of the same supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
shows repeated JNIEnv native-table loads from byte offset `0x1c8`, slot 114,
in exports including `Java_org_videolan_libvlc_MediaPlayer_nativeGetTitles`.
That slot is raw `CallStaticObjectMethod`.

Keep the next slice bounded to:

- exact raw CallStaticObjectMethod slot 114 only;
- live exact jclass + StaticMethod validation;
- reuse of the accepted raw r3-plus-stack descriptor/AAPCS32 decoder;
- a caller-owned synchronous static object-call bridge returning one logical
  jobject identity;
- bounded validation/retention of a non-null returned logical reference.

Leave CallStaticObjectMethodV/A, other return-valued static families, Java
class initialization/dispatch, framework object creation, and broad reference
policy separate.

## Validation

Record the focused machine-code evidence, add host regressions first, then use
exact-head checks. After terminal success, leave CI and converge state.
