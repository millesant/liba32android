# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-object-arrays`.

The jlong-array slice is complete at
`94dd3ed5155654956decce93dd6cbe73c25d4cf0` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives one compact adjacent object-array
family:

1. NewObjectArray at JNIEnv slot 172 / offset `0x2b0`;
2. GetObjectArrayElement at slot 173 / `0x2b4`;
3. SetObjectArrayElement at slot 174 / `0x2b8`;
4. bounded synthetic object-array handles with owned logical jobject vectors;
5. registered element-class identity, generic GetArrayLength metadata, and one
   local array reference;
6. GetObjectArrayElement creates one local reference for each non-null returned
   stored identity;
7. SetObjectArrayElement accepts null or a currently live logical identity;
8. pinned-NDK ARM32 fixture coverage of create/set/get/release.

Do not infer Java class assignability, ArrayStoreException, inheritance, object
construction, local frames, or general GC into this slice.
