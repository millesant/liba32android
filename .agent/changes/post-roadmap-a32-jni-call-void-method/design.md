# Design — ARM32 JNI raw CallVoidMethod bridge

## Evidence boundary

Direct Thumb-2 disassembly of the supplied VLC ARMv7 `libvlcjni.so`
`VLCJniObject_attachEvents` helper loads the JNIEnv function pointer at byte
offset `0xf4`, slot 61, then calls it with JNIEnv, receiver, method ID, and
the first raw variadic word in r0-r3.

The same call site explicitly stages later arguments in the caller stack and
converts one float source value to double before storing it at an 8-byte-aligned
stack offset. This is direct evidence for the raw C variadic entrypoint and its
AAPCS32 continuation shape.

## Decoder

The existing JNI method descriptor remains authoritative for argument types.
The raw decoder starts with one available core word in r3. Promoted 32-bit
integral/reference arguments consume r3 first, then successive guest stack
words. A 64-bit value cannot start in odd r3, so jlong, jdouble, and
default-promoted jfloat advance directly to an 8-byte-aligned guest stack
cursor and consume two little-endian words.

The descriptor parser retains the existing hard/configured argument ceiling and
the CallVoidMethodV validation rules for object descriptors, nested arrays, and
void return type.

## Bridge boundary

CallVoidMethod and CallVoidMethodV normalize to the same
`A32JniValue` vector and invoke the same caller-owned
`A32JniMethodCallBridge`. Receiver/member/reference liveness checks remain
identical. No host pointer is exposed to the guest and no Java method
implementation is synthesized.

## Boundary

This slice does not implement CallVoidMethodA, return-valued calls, static or
nonvirtual calls, NewObject, class dispatch, or Java framework behavior.
