# Compatibility spec delta — ARM32 JNI GetStaticMethodID

- Publish `GetStaticMethodID` at JNIEnv slot 113 / byte offset `0x1c4`
  through one distinct private ARM service stub.
- Add StaticMethod as a distinct bounded member kind while preserving existing
  InstanceMethod, InstanceField, and StaticField identities.
- GetStaticMethodID requires exact configured JNIEnv/attached state, a known
  logical jclass, bounded readable method-name/signature strings, and exact
  StaticMethod class/name/signature identity.
- Exact matches return the caller-seeded logical jmethodID. Unknown class or
  member identity is a semantic miss and returns null. Guest memory faults or
  unterminated strings fail the service.
- Static and instance methods with the same class/name/signature remain
  distinct identities.

Static method invocation, Java inheritance/dispatch, class initialization,
reflection, and framework behavior remain outside this delta.
