# Proposal — ARM32 JNI object arrays

## Intent

Add the next compact array family directly demonstrated by the supplied ARMv7
VLC library while reusing the generic logical-reference model.

## Scope

Publish NewObjectArray, GetObjectArrayElement, and SetObjectArrayElement at
their observed JNIEnv slots. Object arrays store logical jobject handles in
owned bounded vectors, retain their own local array reference, and reuse
GetArrayLength.

Array elements are Java-heap-like stored identities, not JNI local/global
reference counts. GetObjectArrayElement creates a local reference for a non-null
stored identity. SetObjectArrayElement accepts null or a currently live logical
reference. Runtime class-assignability checks and ArrayStoreException remain
outside this slice because the current model has no Java inheritance graph.
