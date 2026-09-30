# ARM32 JNI CallStaticVoidMethod evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlcjni.so`
(SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`).

## Observed entrypoint

`Java_org_videolan_libvlc_Dialog_QuestionDialog_nativePostAction` loads the
JNIEnv function pointer at byte offset `0x234`. ARMv7 JNIEnv entries are
32-bit words, so `0x234 / 4 = 141`, the standard raw
`CallStaticVoidMethod` slot.

The callsite forwards JNIEnv, jclass, and cached static jmethodID in r0-r2. The
first variadic Java argument occupies r3 and remaining raw argument words are
taken from the guest stack under AAPCS32.

## Boundary

This observation establishes raw CallStaticVoidMethod at slot 141. It does not
establish CallStaticVoidMethodV/A, return-valued static calls, Java class
initialization, dispatch/inheritance, or framework method behavior.
