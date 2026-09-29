# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-array-length`.

The strong/local reference slice is complete at
`764658ec7a6bde80b2cc6b0474bba75f0dd79d1b` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next minimal array seam:

1. GetArrayLength at JNIEnv slot 171 / offset `0x2ac`;
2. caller-seeded logical array identities with exact finite jsize-compatible
   lengths;
3. one local reference retained for each seeded array handle;
4. exact GetArrayLength behavior for known arrays and explicit failure for
   null/unknown/non-array handles;
5. real ARM32 fixture execution against a seeded logical array.

Do not infer NewLongArray/NewObjectArray, element storage, region APIs, or
pin/copy semantics into this slice. Reassess those observed entrypoints after
GetArrayLength closes.
