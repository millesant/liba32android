# Proposal — ARM32 JNI thread attachment

## Intent

Add the smallest JavaVM thread-state surface directly demonstrated by the
supplied ARMv7 VLC library: AttachCurrentThread, DetachCurrentThread, and the
already-existing GetEnv behavior across attached/detached state.

## Scope

Publish AttachCurrentThread and DetachCurrentThread at the exact JavaVM
invocation-table slots observed in libmla.so. Extend the existing single guest
JNI context with explicit attached/detached state while preserving the current
logical JNIEnv pointer and bounded guest-memory rules.

This slice does not model multiple host threads, JavaVMAttachArgs contents,
thread names/groups, daemon attachment, or general Java thread state.
