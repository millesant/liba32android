# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-instance-long-fields`.

The object-array slice is complete at
`42d542ab8a14ae11ff534c6f7734f972280741c1` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next exact field pair:

1. GetLongField at JNIEnv slot 101 / offset `0x194`;
2. SetLongField at slot 110 / `0x1b8`;
3. bounded signed int64 values keyed by logical jobject + existing
   InstanceField jfieldID;
4. live-reference validation for service calls;
5. GetLongField returns exact jlong bits through ARM32 r0/r1;
6. SetLongField decodes the aligned value from guest `[sp]` / `[sp+4]`;
7. focused coverage for negative values, updates, missing state, wrong member
   kind, dead/unknown objects, and unreadable stack words.

Do not infer Java object class membership, field layout/offsets, inheritance,
volatile behavior, reflection, or other field families into this slice.
