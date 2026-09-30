# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-exception-check` from
`44ebb794d1b643df8cc54451c59b763fa97f4942`.

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly calls JNIEnv byte offset
`0x390`, slot 228 (ExceptionCheck), and branches on the returned jboolean.

Keep this slice read-only over the existing pending-exception state:

- no pending state -> JNI_FALSE;
- pending state -> JNI_TRUE;
- no new jthrowable reference;
- no clearing or other mutation.

The pinned ARM32 fixture should call the real slot in its no-pending path.
ExceptionDescribe, automatic exception gating, stack traces, Java unwinding,
and framework exception behavior stay separate.

## Validation

After the focused host/fixture coverage is committed, query exact-head commit
checks. Escalate only failing checks, and leave CI immediately on terminal
success.
