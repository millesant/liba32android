# ARM32 JNI weak global reference evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlcjni.so`
(SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`).

## NewWeakGlobalRef

In `VLCJniObject_newFromLibVlc`, the code loads JNIEnv native-table byte
offset `0x388` and calls it with JNIEnv plus the incoming jobject. ARMv7 table
entries are 32-bit words, so `0x388 / 4 = 226`, NewWeakGlobalRef. The returned
handle is stored in the object's native bookkeeping.

## DeleteWeakGlobalRef

In `VLCJniObject_release`, the code loads JNIEnv byte offset `0x38c` and
calls it with the stored weak handle. `0x38c / 4 = 227`, DeleteWeakGlobalRef.

## Boundary

This is balanced create/delete evidence for weak ownership. It does not
establish garbage collection, automatic weak clearing, resurrection,
NewLocalRef-from-jweak, IsSameObject, or local-frame behavior.
