# Compatibility spec delta — ARM32 JNI NewObjectV

- Publish `NewObjectV` at JNIEnv slot 29 / byte offset `0x74` through one
  distinct private ARM service stub.
- Keep raw `NewObject` slot 28 and `NewObjectA` slot 30 null.
- Require exact configured JNIEnv/attached state and a live logical jclass.
- Require an existing InstanceMethod ID owned by that exact class, named
  `<init>`, with a valid void-return method descriptor.
- Decode constructor arguments through the existing bounded ARM32 `va_list`
  decoder and require non-null reference arguments to be live logical
  identities.
- The embedding method-call bridge may return one fresh nonzero logical jobject
  handle; duplicate/invalid handles are rejected and an accepted result gains
  exactly one local reference.

Java heap layout, inheritance/class assignability, constructor bytecode,
NewObject/NewObjectA, and framework object behavior remain outside this delta.
