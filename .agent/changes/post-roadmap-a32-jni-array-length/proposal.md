# Proposal — ARM32 JNI seeded array length

## Intent

Add the smallest array surface directly demonstrated by the supplied ARMv7 VLC
library without introducing array allocation or element marshalling yet.

## Scope

Publish GetArrayLength at its observed JNIEnv slot. Extend the bounded JNI
registry with caller-seeded logical array handles and exact finite lengths.
Seeded arrays are generic logical JNI objects; no APK-specific names or host
pointers enter the runtime.

NewLongArray, NewObjectArray, element access, region APIs, pin/copy semantics,
and release operations remain later slices.
