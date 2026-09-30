# Design — ARM32 JNI raw CallStaticVoidMethod

Direct supplied VLC ARMv7 `libvlcjni.so` evidence identifies JNIEnv slot 141
/ byte offset `0x234` in
`Java_org_videolan_libvlc_Dialog_QuestionDialog_nativePostAction`.

The service reuses the accepted raw ARM32 JNI argument decoder: r3 carries the
first variadic word and later words come from the guest stack, with AAPCS32
alignment for wide/default-promoted values. It requires a live exact jclass and
a StaticMethod member belonging to that class.

Decoded values are passed synchronously to the existing caller-owned
`A32JniMethodCallBridge` through an optional static-void callback. Non-null
reference arguments must be live logical identities. No host pointer or Java
implementation is synthesized.

CallStaticVoidMethodV/A, return-valued static calls, class initialization, Java
dispatch/inheritance, and framework behavior stay separate.
