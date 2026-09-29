# Proposal — ARM32 JNI jlong arrays

## Intent

Add one coherent primitive-array family directly demonstrated by the supplied
ARMv7 VLC library, while keeping JNI state APK-agnostic and bounded.

## Scope

Publish NewLongArray, GetLongArrayElements, ReleaseLongArrayElements, and
SetLongArrayRegion at their observed JNIEnv slots. Store jlong arrays as owned
signed 64-bit values behind synthetic logical guest handles. Reuse the generic
reference ledger and GetArrayLength metadata.

GetLongArrayElements exposes one bounded guest scratch copy at a time.
ReleaseLongArrayElements implements the standard copy-back modes needed for a
copying implementation: mode 0 copies back and releases, JNI_COMMIT copies back
and keeps the lease, and JNI_ABORT discards the scratch copy and releases it.

This slice does not generalize every primitive-array type, critical-array APIs,
object arrays, pinning, or multiple simultaneous element leases.
