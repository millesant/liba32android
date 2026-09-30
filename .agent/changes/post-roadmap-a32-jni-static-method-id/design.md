# Design — ARM32 JNI GetStaticMethodID

## Evidence boundary

Direct Thumb-2 disassembly of supplied VLC ARMv7 `libvlcjni.so` JNI_OnLoad
shows repeated loads from JNIEnv native-table byte offset `0x1c4`. On ARMv7
the table uses 32-bit entries, so `0x1c4 / 4 = 113`, the standard
`GetStaticMethodID` slot. The callsites forward JNIEnv, jclass, method-name,
and signature pointers in r0-r3.

## Member identity

Extend the existing bounded member-kind domain with StaticMethod. Preserve the
existing numeric values for InstanceMethod, InstanceField, and StaticField by
appending the new kind.

Static and instance methods may therefore share the same class/name/signature
while remaining distinct logical IDs. Handles stay caller-seeded 32-bit logical
values; strings remain copied/owned under existing ceilings.

## Service behavior

GetStaticMethodID reuses the existing member lookup path: exact configured
JNIEnv and attached state, known logical class, bounded readable NUL-terminated
name/signature, and exact class/kind/name/signature matching. Semantic misses
return null; memory faults fail.

## Boundary

This slice does not invoke static methods and does not introduce Java
inheritance, reflection, dispatch, class initialization, or framework behavior.
