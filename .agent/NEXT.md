# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-modified-utf8-strings`.

The GetStaticIntField slice is complete at
`2aeb574f67dcd2b01e6593ed9947f50b528b9a76` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next bounded string seam:

1. NewStringUTF at JNIEnv slot 167 / offset `0x29c`;
2. GetStringUTFChars at slot 169 / `0x2a4`;
3. ReleaseStringUTFChars at slot 170 / `0x2a8`;
4. bounded synthetic logical jstring handles and owned byte payloads;
5. one caller-owned guest scratch region and one outstanding UTF-char lease;
6. real pinned-NDK ARM32 JNI_OnLoad round-trip of `"hello"`.

Do not infer UTF-16 APIs, GetStringUTFLength, region APIs, multiple simultaneous
leases, Unicode normalization, or a complete Java String runtime into this
slice.
