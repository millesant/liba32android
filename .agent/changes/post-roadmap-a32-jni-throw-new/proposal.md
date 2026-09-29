# Proposal — ARM32 JNI ThrowNew

## Intent

Add the smallest evidence-backed JNI exception seam without pretending that the
runtime already implements Java exception objects or a full ART exception
machine.

## Scope

Publish ThrowNew at the exact JNIEnv slot observed in the supplied ARMv7 VLC
library. Store at most one pending logical exception as a registered class
handle plus an owned bounded message string.

ThrowNew requires a live logical jclass reference and a bounded readable guest
message. The first pending exception is preserved until the embedding clears it;
a second ThrowNew while one is pending returns JNI_ERR without overwriting it.

This slice does not expose ExceptionOccurred, ExceptionCheck, ExceptionClear,
ExceptionDescribe, throwable object identity, stack traces, Java unwinding, or
automatic exception propagation through guest/native calls.
