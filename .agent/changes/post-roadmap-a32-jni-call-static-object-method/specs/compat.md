# Compatibility spec delta — ARM32 JNI raw CallStaticObjectMethod

- Publish `CallStaticObjectMethod` at JNIEnv slot 114 / byte offset `0x1c8`.
- Use one distinct private ARM service stub.
- Require exact configured JNIEnv/attached state, a live exact logical jclass,
  and an existing StaticMethod ID belonging to that class.
- Reuse the bounded raw r3-plus-stack AAPCS32 argument decoder and accept only
  object/array return descriptors for this service.
- Require every non-null decoded reference argument to be live.
- A successful bridge may return null or a pre-existing logical reference
  identity. A non-null result gains one local reference before guest exposure.
- Unknown returned identities fail; the service does not synthesize Java heap
  identity from arbitrary host callback values.

CallStaticObjectMethodV/A, other return-valued static families, Java class
initialization/dispatch, and framework object creation remain outside this
delta.
