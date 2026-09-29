# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-static-int-field`.

The seeded GetArrayLength slice is complete at
`db558233a50eb79c21e65792dea4a4b74bd72d89` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next minimal Java-state seam:

1. GetStaticIntField at JNIEnv slot 150 / offset `0x258`;
2. caller-seeded signed 32-bit values attached only to existing StaticField
   member IDs;
3. exact class/field-kind/field-handle validation;
4. real ARM32 JNI_OnLoad fixture path through GetStaticFieldID ->
   GetStaticIntField with value 42;
5. no package/class-specific runtime policy.

Do not infer SetStaticIntField, instance field values, Java method invocation,
reflection, inheritance, or framework state into this slice.
