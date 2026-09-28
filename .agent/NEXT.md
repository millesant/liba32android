# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-oss-readiness` is DONE with exact-head local and GitHub Actions
validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e`.

Apache-2.0 licensing is DONE with GitHub recognition and exact-head validation
at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c. Licensing is no longer an OSS-readiness blocker.

## Active JNI track

Continue `post-roadmap-a32-jni-register-natives` from the implementation already
on `main`.

The remaining slice work is:

1. extend the pinned-NDK ARM32 JNI fixture so `JNI_OnLoad` calls FindClass and
   RegisterNatives through the real guest table slots;
2. prove the registered class/name/signature/function association from that
   fixture;
3. invoke one registered guest native through the bounded reverse-dispatch path;
4. converge accepted compatibility spec/architecture/state/evidence;
5. run exact-head required validation before calling the slice complete.

After that, continue the continuous JNI roadmap with class/member identity,
references, strings, arrays, exceptions, method/field calls, thread/VM surface,
direct buffers/critical access/monitors, and remaining evidence-driven slots.

Keep framework classes, graphics/audio/input, application patching, and a
general Java VM outside the generic JNI ABI layer until real evidence requires
those boundaries.
