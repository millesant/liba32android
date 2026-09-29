# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-throw-new`.

The instance-long-field slice is complete at
`114d9d104ee82d1e30aa78fa785c8b389e8630db` with 11/11 checks green.

Direct ARMv7 `libmla.so` evidence drives the next bounded exception seam:

1. ThrowNew at JNIEnv slot 14 / offset `0x38`;
2. one pending logical exception as registered class + owned bounded message;
3. live jclass validation and bounded guest message reads;
4. first ThrowNew returns JNI_OK and records state;
5. duplicate ThrowNew returns JNI_ERR without overwriting the original;
6. host-side clear enables a future ExceptionClear/embedding boundary;
7. focused coverage for table/stub bytes, liveness, bounds, unreadable memory,
   duplicate preservation, clear, and rethrow.

Do not infer Throwable jobjects, stack traces, Java unwinding,
ExceptionOccurred/Check/Clear/Describe, or broad pending-exception restrictions
into this slice.
