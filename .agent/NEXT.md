# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-weak-global-refs` from
`e75ce2a2c2d987ad8a66069982f331ff63e2db6e`.

Balanced VLC ARMv7 evidence proves NewWeakGlobalRef at slot 226 / `0x388` and
DeleteWeakGlobalRef at slot 227 / `0x38c`.

The implementation keeps the same logical object handle and tracks a bounded
weak count separately from local/global strong counts. Weak ownership never
makes an otherwise strong-dead identity live; DeleteWeakGlobalRef remains
possible so stored weak ownership can be released deterministically.

Keep GC, automatic weak clearing, resurrection, NewLocalRef-from-jweak,
IsSameObject, local frames, and Java heap reachability outside this slice.

## Validation

Use exact-head checks after the focused weak-lifetime regression is committed.
Escalate only failing checks. After terminal success, leave CI and converge.
