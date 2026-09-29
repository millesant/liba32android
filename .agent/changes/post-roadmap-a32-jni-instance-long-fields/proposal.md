# Proposal — ARM32 JNI instance long fields

## Intent

Add the smallest instance-field value seam directly demonstrated by the supplied
ARMv7 VLC library, reusing existing logical field IDs and generic object
references without inventing a Java object runtime.

## Scope

Publish GetLongField and SetLongField at their observed JNIEnv slots. Store
bounded signed 64-bit values keyed by logical jobject identity plus existing
InstanceField jfieldID.

GetLongField returns the exact caller/runtime-seeded value as ARM32 64-bit
return bits in r0/r1. SetLongField decodes its aligned jlong argument from the
guest stack according to AAPCS32 and creates/updates the bounded value entry.

This slice does not infer object class membership, field offsets/layout,
inheritance, volatile semantics, reflection, or other primitive/object field
families.
