# NEXT

## JNI evidence discovery

Continue from validated ExceptionCheck revision `918e4b9bef49051e8ea84716e9b759fd11b0fb6c`.

Inspect the supplied ARMv7 VLC artifacts and choose the next bounded JNI seam
only when a direct callsite or balanced lifecycle pair is observed. Do not
preselect ExceptionDescribe, NewLocalRef, IsSameObject, local-frame APIs, GC
behavior, or another method-call family merely because it remains unimplemented.

For the next selected seam:

- record exact JNIEnv slot/byte offset and callsite evidence;
- define the smallest logical state/identity/lifetime contract required;
- add focused host coverage before broadening implementation;
- keep unrelated Java/framework semantics explicitly outside the slice.

## Validation

After implementation, query exact-head checks once. Escalate only a failing or
ambiguous check, and leave CI immediately on terminal success.
