# ARM32 JNI ExceptionOccurred / ExceptionClear evidence — 2026-09-30

## Artifact

The supplied VLC APK
`VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
contains ARMv7 `lib/armeabi-v7a/libvlcjni.so`
(SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`).

## Observed machine code

In `Java_org_videolan_libvlc_Media_nativeNewFromFd`, after the observed
GetIntField call, the function loads two additional JNIEnv function pointers:

- byte offset `0x3c`, which is `0x3c / 4 = 15` → `ExceptionOccurred`;
- byte offset `0x44`, which is `0x44 / 4 = 17` → `ExceptionClear`.

Both are direct table loads from the ARMv7 JNIEnv function table and are used
as ordinary indirect JNI calls in the same export.

## Boundary

The machine code establishes only the presence and exact slots of
ExceptionOccurred and ExceptionClear in this path. It does not establish
ExceptionCheck, ExceptionDescribe, Java stack traces, Java-frame unwinding,
automatic pending-exception gating of unrelated JNI calls, or framework-level
exception behavior.
