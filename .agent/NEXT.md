# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-observed-member-ids`.

Direct ARMv7 `libmla.so` evidence narrows this slice to:

1. GetMethodID at JNIEnv slot 33 / offset `0x84`;
2. GetFieldID at slot 94 / offset `0x178`;
3. GetStaticFieldID at slot 144 / offset `0x240`;
4. bounded caller-seeded logical member IDs with exact
   class/kind/name/signature lookup;
5. focused host regressions and exact-head validation.

Do not add GetStaticMethodID, GetObjectClass, IsInstanceOf, Java method
invocation, field access, reference lifetime, or inheritance semantics without a
separate evidence-backed contract.

After this slice, reassess supplied binaries before selecting the next JNI
surface rather than implementing the roadmap mechanically.
