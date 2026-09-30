# Design — ARM32 JNI raw CallStaticObjectMethod

## Evidence boundary

Direct supplied VLC ARMv7 `libvlcjni.so` callsites load JNIEnv byte offset
`0x1c8`, slot 114, for raw `CallStaticObjectMethod`.

## Decode and identity boundary

The existing descriptor/AAPCS32 decoder is extended only enough to distinguish
void-return signatures from object/array-return signatures. Existing
CallVoidMethod, CallStaticVoidMethod, and NewObjectV paths continue to require
void return descriptors.

CallStaticObjectMethod requires a live exact jclass and a StaticMethod ID owned
by that class. Non-null reference arguments must be live logical identities.

## Return lifetime

The caller-owned synchronous bridge returns either null or a pre-existing
logical JNI reference handle. The service never fabricates Java heap state for
an arbitrary returned number. A non-null handle must already exist in the
registry and receives one local reference before being returned in r0.

## Boundary

V/A variants, other static return types, class initialization/dispatch,
framework object creation, and general Java heap modeling remain separate.
