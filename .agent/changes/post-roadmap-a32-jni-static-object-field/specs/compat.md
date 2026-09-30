# Compatibility spec delta — ARM32 JNI GetStaticObjectField

- Publish `GetStaticObjectField` at JNIEnv slot 145 / byte offset `0x244`
  through one distinct private ARM service stub.
- Caller-seeded state associates null or a pre-existing logical jobject
  identity with an existing StaticField member ID.
- GetStaticObjectField requires exact configured JNIEnv/attached state, a
  registered class, a StaticField member belonging to that class, and an
  explicitly seeded value.
- A seeded null returns null. A non-null stored identity receives exactly one
  local reference before the same opaque logical handle is returned.
- Stored static-field identity is independent of caller local/global counts, so
  deleting a prior returned local reference does not erase the field value.
- Unknown seeded identities, wrong class/member kind, and missing values fail.

SetStaticObjectField, Java class initialization, descriptor type enforcement,
inheritance/assignability, garbage collection/reachability, and framework
object semantics remain outside this delta.
