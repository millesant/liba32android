# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-static-object-field` from
`500ff207eabbdede73f7af4cfab1b5e385a045e8`.

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly proves
`GetStaticObjectField` at JNIEnv slot 145 / byte offset `0x244`.
The returned jobject is immediately used through GetStringUTFChars and later
DeleteLocalRef.

Keep the slice bounded to caller-seeded static object state on an existing
StaticField ID. A seeded null returns null; a non-null known logical identity
receives one local reference when read. Stored static-field identity is not a
caller JNI local/global count and survives deletion of an earlier returned
local reference.

Do not add SetStaticObjectField, Java class initialization, descriptor type
enforcement, inheritance/assignability, garbage collection/reachability, or
framework object semantics.

## Validation

Run exact-head checks after the focused host regression is committed. Escalate
only failing checks. Leave CI immediately on terminal success and converge.
