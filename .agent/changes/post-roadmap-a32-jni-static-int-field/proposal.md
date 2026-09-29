# Proposal — ARM32 JNI static int field read

## Intent

Add the smallest Java field-value surface directly demonstrated by the supplied
ARMv7 VLC library without introducing a general Java object runtime.

## Scope

Publish GetStaticIntField at its observed JNIEnv slot. Reuse the bounded
class/member registry: callers seed exact static-field member IDs and one signed
32-bit value for those IDs. The runtime validates the class/field association
before returning the value.

This slice does not implement SetStaticIntField, instance field access, object
construction, Java method invocation, inheritance, reflection, or automatic
framework/class state.
