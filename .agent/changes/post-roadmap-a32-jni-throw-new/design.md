# Design — ARM32 JNI ThrowNew

## Evidence-backed entry

Direct disassembly of supplied ARMv7 `libmla.so` identifies ThrowNew at JNIEnv
byte offset `0x38`, slot 14.

The wrapper forwards JNIEnv, jclass, and const char* through r0-r2 and receives
the jint result in r0.

## Pending exception model

The existing bounded JNI registry gains one optional pending-exception record:

- registered logical class handle;
- owned message bytes.

The registry limit set gains an explicit hard-capped exception-message ceiling.

A host-side clear method exists so an embedding boundary or future
ExceptionClear implementation can consume/reset the pending state without
inventing a guest-visible API.

## ThrowNew behavior

The service requires:

- exact configured JNIEnv and attached state;
- valid registry;
- registered class handle with at least one live local/global reference;
- bounded readable NUL-terminated guest message.

If no exception is pending, the registry copies the message and records the
class, then returns JNI_OK. If one is already pending, ThrowNew returns JNI_ERR
and preserves the original exception.

## Boundary

This slice records pending exception state only. It does not create a Throwable
jobject, synthesize stack traces, unwind Java frames, restrict all other JNI
calls while pending, or implement ExceptionOccurred/Check/Clear/Describe.
