# Design — ARM32 JNI strong/local references

## Evidence-backed JNIEnv entries

Supplied ARMv7 libmla.so machine code loads:

- NewGlobalRef from byte offset 0x54, slot 21;
- DeleteGlobalRef from byte offset 0x58, slot 22;
- DeleteLocalRef from byte offset 0x5c, slot 23.

The same library contains NewWeakGlobalRef at offset 0x388 / slot 226, but no
matching DeleteWeakGlobalRef wrapper was found in the targeted symbol pass.
Weak references are therefore deferred rather than implemented half-way.

## Reference ledger

The existing bounded JNI registry gains an opaque logical-reference ledger.
Reference values remain 32-bit guest handles; no host pointer becomes a jobject.

Each known object identity stores separate local and global reference counts.
Class registration creates an identity with zero live counts. A successful
FindClass increments the local count and returns the existing logical class
handle.

NewGlobalRef(NULL) returns NULL. For a known live non-null reference it
increments the global count and returns the same logical handle. This
identity-preserving representation is an implementation choice; callers must
still treat JNI references as opaque.

DeleteLocalRef(NULL) and DeleteGlobalRef(NULL) are no-ops. For a known handle
they decrement only the requested count when present. Counts are bounded and
cannot overflow. An identity remains known after all counts reach zero so a
later producer such as FindClass can establish a fresh local reference.

## Deliberate boundary

Reference-count bookkeeping in this slice does not yet turn every class/member
lookup into a liveness check. Class/member metadata is independently retained by
the bounded registry. Full object-lifetime enforcement across all JNI APIs,
local frames, weak references, GC, and thread-local reference ownership remain
later work.
