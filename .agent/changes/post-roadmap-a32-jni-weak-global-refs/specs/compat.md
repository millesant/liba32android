# Compatibility spec delta — ARM32 JNI weak global references

- Extend the JNIEnv table through slot 227 only.
- Publish NewWeakGlobalRef at slot 226 / byte offset `0x388`.
- Publish DeleteWeakGlobalRef at slot 227 / byte offset `0x38c`.
- Use distinct private ARM service stubs.
- Track weak ownership separately from local/global strong reference counts on
  the same logical object identity.
- NewWeakGlobalRef accepts null as null, otherwise requires a currently
  strong-live known identity and increments only the bounded weak count.
- DeleteWeakGlobalRef accepts null as a no-op and otherwise requires and
  decrements existing weak ownership.
- Weak ownership does not satisfy strong-liveness checks and cannot by itself
  seed NewGlobalRef.

Garbage collection, automatic weak clearing, resurrection,
NewLocalRef-from-jweak, IsSameObject, local frames, and Java heap reachability
remain outside this delta.
