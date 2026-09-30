# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-call-void-method` from validated byte-array
revision `4e8326b03a8f9180700e9715126b05081ddee7b9`.

The supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
directly proves raw variadic `CallVoidMethod` at JNIEnv slot 61 / byte offset
`0xf4`. In `VLCJniObject_attachEvents`, the fixed JNI arguments occupy
r0-r2, the first variadic word is forwarded in r3, later words are staged on
the guest stack, and a float source is promoted to an 8-byte-aligned double
stack argument before the indirect call.

Keep this slice bounded to:

- publish only raw `CallVoidMethod` slot 61 while preserving accepted
  `CallVoidMethodV` slot 62 behavior;
- reuse exact JNIEnv, receiver, InstanceMethod ID, descriptor, reference-
  liveness, and caller-owned bridge validation;
- decode the first promoted 32-bit variadic value from r3 and continue on the
  guest stack;
- align jlong/jdouble/default-promoted jfloat to 8-byte stack boundaries;
- normalize into the existing `A32JniValue` bridge vector;
- keep CallVoidMethodA, return-valued/static/nonvirtual families, NewObject,
  Java dispatch, and framework behavior separate.

## Validation

Implement focused host coverage before exact-head checks. After terminal CI
success, leave CI immediately and converge the accepted change state.
