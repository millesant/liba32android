# NEXT

## JNI evidence discovery

Continue from validated weak-global-reference revision `398d217f5bd5e805170f29e1205d561517673d7d`.

Do not preselect NewLocalRef, IsSameObject, local-frame APIs, GC behavior, or
another call family merely because they remain unimplemented. Inspect the
supplied VLC ARMv7 `libvlcjni.so` and choose the next bounded JNI seam only
when a direct callsite or balanced lifecycle pair is observed.

For the next selected seam:

- record exact JNIEnv slot/byte offset and callsite evidence;
- define the smallest logical identity/lifetime contract needed;
- add focused host regressions before mutation broadening;
- keep unrelated Java/framework semantics explicitly outside the slice.

## Validation

After implementation, use exact-head commit checks once. Escalate only a
failing or ambiguous check, and leave CI immediately on terminal success.
