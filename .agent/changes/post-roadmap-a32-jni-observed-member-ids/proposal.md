# Proposal — ARM32 JNI observed member IDs

## Intent

Advance JNI with the smallest member-identity surface directly demonstrated by
the supplied ARMv7 VLC library, without inventing a broader Java object model.

## Scope

Publish GetMethodID, GetFieldID, and GetStaticFieldID at the exact JNIEnv slots
observed in libmla.so. Extend the caller-owned bounded JNI registry with opaque
logical 32-bit member IDs and exact class/kind/name/signature lookup.

This slice does not implement GetStaticMethodID, GetObjectClass, IsInstanceOf,
field access, Java method invocation, reference lifetime, framework classes, or
automatic Java reflection/state.
