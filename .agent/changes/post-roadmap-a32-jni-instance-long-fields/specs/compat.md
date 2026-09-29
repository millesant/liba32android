# Compatibility spec delta — ARM32 JNI instance long fields

The JNIEnv table publishes:

- GetLongField at slot 101 / byte offset `0x194`;
- SetLongField at slot 110 / byte offset `0x1b8`.

Each targets a distinct private ARM service stub.

The bounded registry may associate one signed 64-bit value with an exact
logical jobject identity and existing InstanceField jfieldID. The object
identity must exist in the generic reference ledger; the field ID must resolve
to an InstanceField member. Existing pairs update deterministically and new
pairs obey the configured hard-capped member count.

At service time the jobject must be currently live. GetLongField returns an
existing value as ARM32 jlong bits in r0/r1 and fails when state is missing.
SetLongField reads the aligned 64-bit value from guest `[sp]`/`[sp+4]`,
creates or updates the pair, and returns normally.

This slice does not infer jobject class membership, field offsets/layout,
inheritance, volatile semantics, reflection, or other field families.
