# Design — ARM32 JNI static int field read

## Evidence-backed entry

Supplied ARMv7 libmla.so machine code for
`_JNIEnv::GetStaticIntField(_jclass*, _jfieldID*)` loads its function pointer
from JNIEnv byte offset `0x258`. `0x258 / 4 == 150`, so the guest table
publishes GetStaticIntField at slot 150.

## Registry model

The existing bounded member registry already stores exact
class/kind/name/signature identity and opaque logical member handles. This slice
adds a bounded static-int value table keyed by member handle.

A value may be seeded only for an existing StaticField member. Re-seeding the
same field updates its value without adding a second record. The number of
stored values cannot exceed the existing member-ID ceiling.

## Service behavior

GetStaticIntField requires:

- the exact configured JNIEnv pointer and an attached context;
- a registered class handle in r1;
- a registered StaticField member handle in r2;
- that member to belong to the supplied class;
- a seeded signed 32-bit value for the field.

Success returns the exact jint bits in r0. Unknown class/member identities,
wrong member kind/class, or missing value fail rather than fabricating Java
state.

## Deliberate boundary

The runtime remains APK-agnostic. Values are caller-supplied logical Java state;
no VLC/package names, reflection, host object pointers, or framework behavior are
embedded in src/compat.
