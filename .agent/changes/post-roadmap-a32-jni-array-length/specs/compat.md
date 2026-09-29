# Compatibility spec delta — ARM32 JNI seeded array length

The JNIEnv native table publishes GetArrayLength at slot 171 / byte offset
0x2ac using one distinct private ARM service stub.

The bounded JNI registry may seed finite logical arrays. Each array has one
nonzero logical handle and an exact length no greater than INT32_MAX. Seeding an
array creates/reuses its generic reference identity and retains one local
reference.

GetArrayLength requires the exact configured JNIEnv pointer, attached JNI
context, and known array handle. It returns the exact seeded jsize in r0. Null,
unknown, and non-array handles fail.

No array allocation, element storage/access, region operations, pin/copy
semantics, or Java type inference are introduced.
