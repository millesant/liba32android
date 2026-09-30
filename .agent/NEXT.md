# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-exception-observe-clear` from validated
GetIntField revision `3e2323a85b396e85dea0e84331d76e9d18fe3aab`.

The supplied VLC ARMv7 `libvlcjni.so` directly proves:

- `ExceptionOccurred` at JNIEnv slot 15 / byte offset `0x3c`;
- `ExceptionClear` at JNIEnv slot 17 / byte offset `0x44`.

This slice adds a bounded logical pending-exception identity so
ExceptionOccurred can return a real logical jthrowable handle without exposing
a host pointer or reusing the jclass handle. ExceptionClear clears the pending
root while preserving any local/global reference already returned.

Keep ExceptionCheck, ExceptionDescribe, stack traces, Java unwinding,
automatic pending-exception gating across all JNI calls, and framework
exception behavior separate.

## Validation

Run the focused JNI host regression through the exact-head check surface.
Escalate to job steps/logs only on failure. After terminal success, leave CI
and converge the change state.
