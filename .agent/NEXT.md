# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-android-apk-runtime-bootstrap` is DONE at
`268d151bb8c93473c27abdefb9b9644b51ff3c96`.

`post-roadmap-android-apk-native-catalog` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The APK source now shares one bounded ZIP32 central-directory parser between
exact-entry loading and native catalog enumeration. For a caller-selected ABI
directory, direct `.so` basenames are discovered under independent
library/per-name/total-name ceilings, duplicate names fail, output is
lexicographically deterministic, and the real ARM32 bootstrap consumes the
discovered set instead of a manually typed membership list.

The supplied VLC APK produces the expected four-member ARMv7 catalog:
`libc++_shared.so`, `libmla.so`, `libvlc.so`, and `libvlcjni.so`.
The caller still chooses the APK, ABI directory, and initial root SONAME.

After terminal success, stop deepening linker/archive policy and open the first
JNI compatibility slice. Start with the smallest guest-visible ARM32 JNI VM
bootstrap needed to call a library's `JNI_OnLoad`: bounded JavaVM/JNIEnv
pointer-table representation, explicit supported JNI version negotiation,
`JavaVM::GetEnv` service dispatch, and lifecycle-safe guest invocation of an
exported `JNI_OnLoad(JavaVM*, void*)` through the existing ARM32 executor.
Use real library evidence to decide which additional JNI functions are required;
do not pre-build a speculative Java object model.

Keep FindClass/RegisterNatives/native-method dispatch, Java object/string/array
semantics, Android framework classes, graphics/audio, ABI auto-detection,
manifest/root selection, split APKs, package discovery, app patching, and
device deployment separate until JNI_OnLoad bootstrap proves the next seam.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
