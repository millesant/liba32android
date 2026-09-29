# Proposal — ARM32 JNI modified-UTF-8 strings

## Intent

Add the smallest APK-agnostic JNI string surface directly demonstrated by the
supplied ARMv7 VLC library.

## Scope

Publish NewStringUTF, GetStringUTFChars, and ReleaseStringUTFChars at their
observed JNIEnv slots. NewStringUTF creates bounded logical jstring identities
with owned byte-preserving modified-UTF-8 payloads. GetStringUTFChars exposes
one bounded guest scratch copy at a time and ReleaseStringUTFChars closes that
lease.

This slice does not implement UTF-16 jstring APIs, GetStringUTFLength,
GetStringRegion/GetStringUTFRegion, multiple simultaneous UTF leases, Unicode
normalization, or a complete modified-UTF-8 validator.
