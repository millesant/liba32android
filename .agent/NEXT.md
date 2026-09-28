# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-android-apk-native-catalog` is DONE at
`eea5433f77049ed1cccddd5998213b6816194e63`.

`post-roadmap-a32-jni-onload-bootstrap` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The first JNI slice now installs a logical ARM32 JavaVM/JNIEnv ABI, publishes
JavaVM::GetEnv at invoke-table slot 6, matches Dalvik GetEnv version/output
ordering, and invokes exact-object JNI_OnLoad through the existing bounded A32
service executor while preserving lifecycle object provenance.

A dedicated pinned-NDK fixture proves the real guest ABI: JNI_OnLoad performs an
indirect GetEnv through JavaVM offset `0x18`, receives a non-null JNIEnv, and
returns JNI 1.6.

After terminal success, continue with the next evidence-backed JNI seam rather
than expanding unrelated loader policy:

1. expand the caller-addressed JNINativeInterface table far enough to publish
   exact real slots;
2. add a bounded caller-owned class registry and `JNIEnv::FindClass` at native
   table slot 6 / offset `0x18`;
3. add bounded `JNIEnv::RegisterNatives` at slot 215 / offset `0x35c`;
4. represent registered methods as guest logical function pointers plus owned
   class/name/signature metadata under explicit count/string ceilings;
5. prove registration from a generated ARM32 JNI_OnLoad fixture before adding
   actual Java-to-native method invocation.

The supplied `libmla.so` machine code directly proves both table offsets above,
so this is the smallest real-library-driven next step.

Keep Java object/reference semantics, strings/arrays/exceptions, method
invocation, Attach/Detach, JNI_OnUnload, framework classes, graphics/audio,
app patching, device deployment, and a general Java VM outside that next slice.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
