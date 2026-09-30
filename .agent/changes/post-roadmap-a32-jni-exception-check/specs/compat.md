# Compatibility spec delta — ARM32 JNI ExceptionCheck

- Extend the JNIEnv table through slot 228 only.
- Publish `ExceptionCheck` at slot 228 / byte offset `0x390` through one
  distinct private ARM service stub.
- Require the exact configured JNIEnv and the existing attached bounded JNI
  context.
- Return JNI_FALSE when no pending exception exists.
- Return JNI_TRUE when the accepted bounded pending-exception state exists.
- Do not create a jthrowable reference, alter the pending identity, or clear
  pending state.

ExceptionDescribe, automatic exception gating, stack traces, Java unwinding,
and framework exception behavior remain outside this delta.
