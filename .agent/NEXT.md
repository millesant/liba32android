# NEXT

## Active JNI track

Continue from validated GetIntField revision `1408748de1a226ee3e0dce544e52237d8765530e`.

The same supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
directly proves two adjacent exception calls in
`Java_org_videolan_libvlc_Media_nativeNewFromFd`:

- `ExceptionOccurred` at JNIEnv slot 15 / byte offset `0x3c`;
- `ExceptionClear` at JNIEnv slot 17 / byte offset `0x44`.

Before publishing either entry, define the smallest coherent pending-exception
identity model. Existing ThrowNew deliberately stores class/message state
without fabricating a Throwable jobject, so ExceptionOccurred must not simply
return the class handle or another invented identity.

Keep the next slice bounded to observed exception observation/clear semantics.
Do not fold in ExceptionCheck, ExceptionDescribe, Java stack traces, unwinding,
automatic propagation through every JNI service, or framework exception types.

## Validation

Add focused state-transition regressions first. Use exact-head checks only after
the bounded implementation is coherent; after terminal success, leave CI and
converge evidence/state.
