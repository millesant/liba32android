# Design — ARM32 JNI seeded array length

## Evidence-backed entry

Supplied ARMv7 libmla.so machine code loads JNIEnv byte offset 0x2ac before the
GetArrayLength indirect call. 0x2ac / 4 == 171, so the guest table publishes
GetArrayLength at slot 171.

## Registry model

The existing bounded JNI registry gains finite array metadata:

- one nonzero logical jobject/jarray handle;
- one length representable by signed 32-bit jsize;
- no host pointer identity.

add_array seeds an array identity and one local reference for the caller-visible
guest handle. Existing reference identities may be reused; new identities are
created under the existing reference-handle ceiling.

## Service behavior

GetArrayLength requires the exact configured JNIEnv pointer, an attached context,
and a registered array handle. It returns the exact seeded jsize in r0. Null,
unknown, or non-array handles fail the service rather than fabricating a length.

## Deliberate boundary

This slice does not allocate arrays, store elements, implement object/primitive
array element access, pin/copy buffers, or infer Java array types.
