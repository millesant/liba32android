# Design — ARM32 JNI instance int field reads

## Evidence boundary

Direct Thumb-2 disassembly of the supplied VLC ARMv7 `libvlcjni.so`
`Java_org_videolan_libvlc_Media_nativeNewFromFd` export loads the JNIEnv
function pointer at byte offset `0x190`, slot 100, then invokes it with
JNIEnv, jobject, and a cached jfieldID in r0-r2. The returned jint is consumed
from r0.

This slice therefore publishes GetIntField only. The adjacent
ExceptionOccurred/ExceptionClear calls visible in the same function are
recorded as separate future evidence rather than folded into field behavior.

## State model

The existing bounded member-ID registry already distinguishes InstanceField
handles. Add caller-seeded signed 32-bit values keyed by logical jobject handle
plus an existing InstanceField handle. The logical object must exist in the
reference ledger; storage never derives or exposes a host object address.

Existing key pairs update deterministically. New pairs obey the same bounded
member-state ceiling used by the accepted instance-long field seam.

## Service behavior

GetIntField requires exact JNIEnv/attached state, a currently live logical
jobject, an existing InstanceField member ID, and an existing instance-int
value for that object/member pair. It returns the signed value bit-for-bit in
r0. Missing or mismatched state fails rather than fabricating a Java field.

## Boundary

Object-class assignability, Java field offsets/layout, inheritance, volatile
semantics, SetIntField, reflection, and other field families remain separate.
