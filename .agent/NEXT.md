# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## JNI track

`post-roadmap-a32-jni-register-natives` is DONE with exact-head validation at
`dbbcb06d5a9e0225b9e0a9c3515b646c0fff6503`. The pinned-NDK ARM32 fixture
now proves JNI_OnLoad -> GetEnv -> FindClass -> RegisterNatives, followed by
host-to-guest invocation of the registered `nativePing()I` method returning 42.

The next bounded JNI slice should establish the class/member identity substrate:

1. deterministic logical guest handles for class/member identities;
2. GetObjectClass and IsInstanceOf behavior over the bounded registry/model;
3. GetMethodID/GetStaticMethodID/GetFieldID/GetStaticFieldID with exact
   class/name/signature lookup and explicit ceilings;
4. focused host regressions plus real ARM32 evidence before broadening further.

After that, continue with reference lifetime, strings, arrays, exceptions,
method/field calls, thread/VM surface, direct buffers/critical access/monitors,
and remaining evidence-driven slots.

Keep framework classes, graphics/audio/input, application patching, and a
general Java VM outside the generic JNI ABI layer until real evidence requires
those boundaries.
