# NEXT

## JNI evidence discovery

Continue from validated GetStaticObjectField revision `82021c5e8aac437e6f1c74cc8db4e797737d74bf`.

The direct VLC ARMv7 JNI_OnLoad seam at slot 145 / `0x244` is complete.
Inspect the supplied ARMv7 artifacts for the next bounded JNI call or balanced
lifecycle pair. Select a new API only from a concrete callsite with an exact
JNIEnv/JavaVM slot or other equally direct ABI evidence.

Do not preselect SetStaticObjectField, local-frame APIs, NewLocalRef,
IsSameObject, GC behavior, or a return-valued method family merely because it
remains unimplemented.

For the next selected seam:

- record exact slot/byte offset and callsite evidence;
- define the smallest logical state/identity/lifetime contract required;
- add focused host coverage before broadening implementation;
- keep unrelated Java/framework behavior outside the slice.

## Validation

After implementation, query exact-head checks once. Escalate only a failing or
ambiguous check, and leave CI immediately on terminal success.
