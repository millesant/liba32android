# NEXT

## Active JNI track

Open `post-roadmap-a32-jni-weak-global-refs` from validated raw
CallStaticObjectMethod revision `e3423411f216203feaca107e7254033f90e4284f`.

Balanced evidence exists in the supplied VLC ARMv7 `libvlcjni.so`
(`sha256:e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`):

- `VLCJniObject_newFromLibVlc` loads JNIEnv byte offset `0x388`, slot 226
  (`NewWeakGlobalRef`), and stores the returned weak handle;
- `VLCJniObject_release` loads byte offset `0x38c`, slot 227
  (`DeleteWeakGlobalRef`), and releases that stored handle.

Keep the pair together. Extend the native table only as far as required by
slots 226/227. Define weak ownership separately from local/global strong
liveness: creating a weak reference must not make an otherwise dead referent
strongly live, and deleting the weak reference must be deterministic.

Before mutation, settle the bounded alias/identity contract for a weak handle
and add focused lifecycle tests. Do not infer GC, resurrection,
NewLocalRef-from-jweak, IsSameObject, local frames, or full Java heap reachability
from this evidence.

## Validation

Implement focused host coverage first, then use exact-head commit checks once.
After terminal success, leave CI and converge state.
