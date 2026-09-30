# Design — ARM32 JNI ExceptionOccurred / ExceptionClear

## Evidence boundary

Direct Thumb-2 disassembly of the supplied VLC ARMv7 `libvlcjni.so`
`Java_org_videolan_libvlc_Media_nativeNewFromFd` export loads JNIEnv byte
offsets `0x3c` and `0x44`, identifying slots 15 and 17:
`ExceptionOccurred` and `ExceptionClear`.

## Pending identity model

The accepted ThrowNew slice already owns bounded pending class/message state but
intentionally did not expose a Throwable jobject. This slice adds one logical
32-bit exception handle allocated from a dedicated bounded namespace.

Creating a pending exception reserves the handle in the reference ledger with
zero local/global counts. The pending root itself keeps the exception identity
meaningful; it is not represented as an implicit guest local reference.

## Observation and clear

ExceptionOccurred leaves pending state intact. With no pending exception it
returns null. Otherwise it retains one local reference to the exact pending
handle and returns that handle.

ExceptionClear clears only the pending root. Local/global references already
returned for the exception remain valid. If the pending identity was never
observed and therefore has zero reference counts, clear reclaims the reserved
identity immediately.

A later ThrowNew must not alias a prior exception identity that is still
present in the logical reference ledger.

## Boundary

This slice does not implement ExceptionCheck, ExceptionDescribe, Java stack
traces, Java-frame unwinding, automatic exception propagation/gating across all
JNI calls, or framework exception types.
