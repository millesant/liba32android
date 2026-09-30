# Design — ARM32 JNI NewObjectV construction bridge

## Evidence boundary

Direct Thumb-2 disassembly of supplied ARMv7 `libmla.so`
`_JNIEnv::NewObject(_jclass*, _jmethodID*, ...)` shows the C++ wrapper
building a `va_list`, loading the JNIEnv function pointer from byte offset
`0x74` (slot 29), and forwarding JNIEnv, jclass, jmethodID, and `va_list`
in r0-r3.

That establishes NewObjectV only. Raw NewObject slot 28 and NewObjectA slot 30
remain null.

## Constructor boundary

Reuse the accepted method descriptor and ARM32 `va_list` decoder. Require a
live exact jclass, an InstanceMethod ID belonging to that class, name
`<init>`, and a void-return constructor descriptor. Non-null reference
arguments must already be live logical identities.

The existing embedding-owned method-call bridge gains an optional constructor
callback. A successful callback supplies a fresh nonzero logical jobject
handle. The registry rejects collisions and retains exactly one local
reference. No host object pointer crosses the guest boundary.

## Boundary

This does not implement Java heap layout, class assignability/inheritance,
constructor bytecode, raw NewObject, NewObjectA, or framework object behavior.
