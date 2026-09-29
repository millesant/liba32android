# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-strong-references`.

The JavaVM AttachCurrentThread/DetachCurrentThread slice is complete at
`35168f13294de7f88ed7b7054f0b08f1c9f5e7e9` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next bounded reference seam:

1. NewGlobalRef at JNIEnv slot 21 / offset `0x54`;
2. DeleteGlobalRef at slot 22 / `0x58`;
3. DeleteLocalRef at slot 23 / `0x5c`;
4. bounded APK-agnostic logical object identities with separate local/global
   counts;
5. FindClass establishes one local reference for its returned class handle;
6. real ARM32 JNI_OnLoad fixture promotes the class to global, deletes the local,
   registers the native through the global handle, then releases the global.

Defer weak references, NewLocalRef, IsSameObject, local frames, GC, and general
object lifetime until a balanced evidence-backed boundary exists.
