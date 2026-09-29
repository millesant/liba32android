# Compatibility spec delta — ARM32 JNI GetStaticIntField

The JNIEnv native table publishes GetStaticIntField at slot 150 / byte offset
0x258 using one distinct private ARM service stub.

The bounded JNI registry may associate one signed 32-bit static value with an
existing StaticField member ID. The member must already belong to a registered
class. Values are bounded by the existing member-ID ceiling and remain
APK-agnostic caller-supplied logical Java state.

GetStaticIntField requires the exact configured JNIEnv pointer, attached state,
a registered class, a StaticField member belonging to that class, and a seeded
value. Success returns the exact jint bits in r0. Unknown/mismatched identities
or missing values fail.

SetStaticIntField, instance field access, Java method invocation, object
construction, inheritance, reflection, and framework state remain outside this
slice.
