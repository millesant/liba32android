# ARM32 JNI CallStaticObjectMethod evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlcjni.so`
(SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`).

## Observed callsites

Direct Thumb-2 inspection shows repeated loads from JNIEnv native-table byte
offset `0x1c8`, including static object-return paths such as
`Java_org_videolan_libvlc_MediaPlayer_nativeGetTitles`.

ARMv7 JNIEnv table entries are 32-bit words, so `0x1c8 / 4 = 114`, the
standard raw `CallStaticObjectMethod` slot. The raw calling convention places
JNIEnv, jclass, and cached static jmethodID in r0-r2, starts variadic Java
arguments in r3, and continues on the guest stack.

## Boundary

This establishes raw CallStaticObjectMethod slot 114 only. It does not establish
V/A variants, other static return types, Java class initialization/dispatch, or
framework object creation semantics.
