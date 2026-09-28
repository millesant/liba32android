# Next Work

The numbered 011-049 roadmap is COMPLETE.

## Immediate repository gate

Finish `post-roadmap-oss-readiness`:

1. validate the public repository/docs hygiene checks;
2. run the normal host build and CTest suite on the exact resulting revision;
3. verify the pushed `bleeding` head and CI state;
4. close the change only after observed validation supports it.

The remaining non-technical OSS-release blocker is the project license. The
maintainer must explicitly select and commit a license before the repository
should be presented as a formally licensed open-source release.

## Active JNI track

Continue `post-roadmap-a32-jni-register-natives` from the implementation already
on `bleeding`.

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
