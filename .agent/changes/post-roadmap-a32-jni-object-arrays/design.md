# Design — ARM32 JNI object arrays

## Evidence-backed entries

Direct disassembly of supplied ARMv7 `libmla.so` identifies:

- NewObjectArray at JNIEnv offset `0x2b0`, slot 172;
- GetObjectArrayElement at `0x2b4`, slot 173;
- SetObjectArrayElement at `0x2b8`, slot 174.

These are adjacent to the already-supported GetArrayLength slot 171.

## Storage

The bounded registry creates synthetic logical array handles using the same
collision-free dynamic-array handle range as primitive arrays. Each object array
stores:

- its logical handle;
- one registered element-class handle;
- a bounded vector of logical jobject element handles.

The array itself starts with one local JNI reference and participates in generic
array-length metadata.

NewObjectArray requires a registered class handle and a nonnegative bounded
length. A null initial element is valid. A non-null initial element must be a
known live logical reference at call time. The initial handle is copied into
all array entries without changing JNI local/global counts.

## Element access

GetObjectArrayElement validates the array and signed index. Null stored elements
return null. A non-null stored identity gains one local JNI reference before the
same opaque logical handle is returned.

SetObjectArrayElement validates array/index. Null clears the element. A non-null
value must be a known live reference at call time, then its logical handle is
stored without changing the caller's JNI reference count.

## Boundary

The current runtime has no Java inheritance/type graph, so this slice does not
perform assignability checks or synthesize ArrayStoreException. Object
construction, method calls, object fields, local frames, and general GC remain
separate.
