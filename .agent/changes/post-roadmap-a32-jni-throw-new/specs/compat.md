# Compatibility spec delta — ARM32 JNI ThrowNew

The JNIEnv table publishes ThrowNew at slot 14 / byte offset `0x38` through one
distinct private ARM service stub.

The bounded registry stores at most one pending logical exception containing a
registered class handle and an owned bounded message string. The exception
message ceiling is explicit and hard-capped.

ThrowNew requires a live logical jclass reference and one readable bounded
NUL-terminated guest message. When no exception is pending, it records the
class/message and returns JNI_OK. When an exception is already pending, it
returns JNI_ERR and preserves the original state.

The embedding may clear pending state through a host-side registry method for a
future Java/ExceptionClear boundary.

Throwable jobject identity, stack traces, Java unwinding, ExceptionOccurred,
ExceptionCheck, ExceptionClear, ExceptionDescribe, and global pending-exception
restrictions remain outside this slice.
