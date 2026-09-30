# ARM32 JNI ExceptionCheck entrypoint evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlc.so`
(SHA-256 `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`).

Its ARM `JNI_OnLoad` starts at `0x002a2ee4`.

## Observed call

The entrypoint performs this direct JNIEnv-table sequence:

- `0x002a2f60`: load the JNIEnv native-table pointer;
- `0x002a2f64`: load the function pointer from byte offset `0x390`;
- `0x002a2f68`: indirect call;
- `0x002a2f6c`: compare r0 with zero and branch on the result.

ARMv7 JNIEnv entries are 32-bit words, so `0x390 / 4 = 228`, the standard
`ExceptionCheck` slot. The same offset appears repeatedly in JNI_OnLoad around
other cleanup/registration paths.

## Boundary

This establishes only the read-only ExceptionCheck observation seam. It does
not establish ExceptionDescribe, Java stack traces/unwinding, or an obligation
to block every unrelated JNI operation while an exception is pending.
