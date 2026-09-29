# Design — ARM32 JNI instance long fields

## Evidence-backed entries

Direct disassembly of supplied ARMv7 `libmla.so` identifies:

- GetLongField from JNIEnv byte offset `0x194`, slot 101;
- SetLongField from JNIEnv byte offset `0x1b8`, slot 110.

The SetLongField wrapper follows AAPCS32 alignment for its jlong argument:
JNIEnv/object/field consume r0-r2, while the 64-bit value is emitted as two
little-endian 32-bit words at guest `[sp]` and `[sp+4]`.

GetLongField returns jlong bits through r0 (low word) and r1 (high word).

## Value model

The existing registry already owns exact InstanceField jfieldID metadata and a
generic logical-reference ledger. This slice adds a bounded value table keyed by
`(object_handle, field_handle)`.

Caller seeding and SetLongField both use the same setter. The object identity
must exist in the logical-reference ledger, and the field handle must resolve to
an existing InstanceField member. Updating an existing key is deterministic;
new keys are capped by the member limit.

At service time the jobject must also be currently live (at least one local or
global JNI reference). GetLongField fails for missing value state rather than
fabricating host/Java state. SetLongField creates or updates the pair.

## Boundary

The runtime does not yet associate arbitrary jobject identities with Java
classes, so this slice cannot prove object/class assignability for a field ID.
It also does not model Java field offsets, inheritance, volatile access,
exceptions, or wider primitive/object field families.
