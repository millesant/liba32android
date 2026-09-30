# Design — ARM32 JNI GetStaticObjectField

## Evidence boundary

The supplied VLC ARMv7 `libvlc.so` JNI_OnLoad first resolves a static field
with JNIEnv offset `0x240` (GetStaticFieldID), then loads offset `0x244`
and calls it with JNIEnv, the same jclass, and the returned jfieldID. ARMv7
JNIEnv entries are four bytes, so `0x244 / 4 = 145`:
`GetStaticObjectField`.

The returned r0 value is immediately consumed as an object by the existing
GetStringUTFChars path and later released through DeleteLocalRef.

## Bounded field state

Add caller-seeded storage keyed by an existing StaticField ID. The stored value
is null or one pre-existing logical JNI identity. The field storage is modeled
as Java/static state rather than as a JNI local/global reference count, so the
stored identity remains available after a local reference returned by an
earlier read is deleted.

No host object pointer is stored or exposed.

## Read semantics

GetStaticObjectField validates exact configured JNIEnv/attachment, registered
jclass, exact StaticField ownership, and seeded state. Null returns null.
A non-null stored identity must still exist in the bounded logical identity
ledger; the service creates one local reference and returns the same handle.

## Boundary

This slice does not implement SetStaticObjectField, Java class initialization,
field descriptor type enforcement, inheritance/assignability, garbage
collection, automatic weak clearing, or framework object creation.
