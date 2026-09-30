# Design — ARM32 JNI ExceptionCheck

## Evidence boundary

Supplied VLC ARMv7 `libvlc.so`
(SHA-256 `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`)
uses JNIEnv byte offset `0x390` repeatedly in `JNI_OnLoad`.

At `0x002a2f60` it loads the JNIEnv function table, at `0x002a2f64` loads
the function pointer from `[table + 0x390]`, calls it, and immediately compares
r0 with zero. `0x390 / 4 = 228`, the standard `ExceptionCheck` slot.

## State model

No new exception object model is needed. The accepted ThrowNew /
ExceptionOccurred / ExceptionClear slices already own one bounded pending
exception state.

ExceptionCheck is a pure observation:

- no pending exception -> JNI_FALSE / r0 = 0;
- pending exception -> JNI_TRUE / r0 = 1.

It does not create a local reference, mutate the logical exception identity, or
clear pending state.

## Boundary

This slice does not implement ExceptionDescribe, automatic exception gating of
unrelated JNI calls, Java stack traces, Java unwinding, or framework exception
semantics.
