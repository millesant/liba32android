# Compatibility spec delta — ARM32 JNI strong/local references

The JNIEnv native table publishes:

- NewGlobalRef at slot 21 / byte offset 0x54;
- DeleteGlobalRef at slot 22 / byte offset 0x58;
- DeleteLocalRef at slot 23 / byte offset 0x5c.

Each points at a distinct private ARM service stub. Unsupported entries remain
zero.

The caller-owned bounded JNI registry maintains logical object-reference
identities with separate local/global counts under explicit handle/count limits.
No host pointer is exposed.

Registering a class creates its logical reference identity with zero live
counts. Successful FindClass retains one local reference before returning that
class handle.

NewGlobalRef on NULL returns NULL. On a known live handle it increments the
global count and returns the same opaque logical handle. DeleteLocalRef and
DeleteGlobalRef accept NULL as no-op and otherwise decrement only their
respective count when present.

This slice does not implement weak references, NewLocalRef, IsSameObject, local
frames, GC, cross-thread local-reference ownership, general object allocation,
or universal reference-liveness enforcement across every JNI entrypoint.
