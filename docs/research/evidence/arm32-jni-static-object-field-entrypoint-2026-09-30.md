# ARM32 JNI GetStaticObjectField evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlc.so`
(SHA-256 `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`).

## Observed JNI_OnLoad path

`JNI_OnLoad` is at ARM32 address `0x2a2ee4`. In the observed path:

- `0x2a2fe0` loads JNIEnv byte offset `0x240`, slot 144,
  `GetStaticFieldID`, and the call returns a cached jfieldID;
- after an ExceptionCheck observation, `0x2a301c` loads JNIEnv byte offset
  `0x244`, slot 145;
- the indirect call uses JNIEnv in r0, the jclass in r1, and the cached
  jfieldID in r2, matching `GetStaticObjectField(JNIEnv*, jclass, jfieldID)`;
- the returned r0 object is then consumed by the existing JNIEnv
  `GetStringUTFChars` entry at offset `0x2a4`, later released through
  `ReleaseStringUTFChars`, and the local object reference is deleted through
  `DeleteLocalRef` at offset `0x5c`.

ARMv7 JNIEnv entries are four-byte function pointers, so
`0x244 / 4 = 145`.

## Boundary

This establishes GetStaticObjectField slot 145 and object-return use in the
observed path. It does not establish SetStaticObjectField, Java class
initialization, field descriptor enforcement, garbage collection/reachability,
or framework-level object behavior.
