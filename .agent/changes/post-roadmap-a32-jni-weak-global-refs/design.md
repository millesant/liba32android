# Design — ARM32 JNI weak global references

## Evidence boundary

Supplied VLC ARMv7 `libvlcjni.so` provides balanced lifecycle evidence:
`VLCJniObject_newFromLibVlc` loads JNIEnv offset `0x388` (slot 226,
NewWeakGlobalRef), and `VLCJniObject_release` loads `0x38c` (slot 227,
DeleteWeakGlobalRef).

## Bounded ownership model

The adapter keeps one logical 32-bit object identity and tracks three reference
counts: local strong, global strong, and weak. A jweak uses the same logical
handle as its referent; reference kind is expressed by the operation and ledger
count rather than by fabricating another guest pointer.

NewWeakGlobalRef requires a currently strong-live identity and increments only
the bounded weak count. Weak count never participates in existing strong
liveness checks. DeleteWeakGlobalRef decrements only weak ownership.

Once all local/global counts reach zero, a remaining weak count does not make
the referent strongly live. In this bounded no-GC model the bookkeeping handle
remains available solely so DeleteWeakGlobalRef can deterministically release
the weak ownership.

## Boundary

No garbage collector, automatic weak nullification, resurrection,
NewLocalRef-from-jweak, IsSameObject semantics, local frames, or Java heap
reachability are introduced.
