# Compatibility spec delta — ARM32 JNI exception observation and clear

- Publish `ExceptionOccurred` at JNIEnv slot 15 / byte offset `0x3c`.
- Publish `ExceptionClear` at JNIEnv slot 17 / byte offset `0x44`.
- Each entry uses a distinct private ARM service stub.
- A successful ThrowNew reserves one bounded logical exception handle in a
  dedicated namespace with zero initial local/global reference counts.
- ExceptionOccurred returns null with no pending exception; otherwise it
  retains one local reference to the exact pending handle, returns that handle,
  and leaves pending state intact.
- ExceptionClear succeeds as an empty-state no-op and otherwise clears the
  pending root without invalidating already returned local/global references.
- Clearing an unobserved zero-reference pending exception reclaims its reserved
  logical identity.
- A later exception does not reuse an identity that is still retained in the
  reference ledger.

ExceptionCheck, ExceptionDescribe, stack traces, Java unwinding, automatic
pending-exception gating, and framework exception behavior remain outside this
delta.
