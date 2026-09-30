# ARM32 JNI GetStaticMethodID evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libvlcjni.so`
(SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`).

Its JNI_OnLoad export starts at Thumb address `0x00006209`
(code address `0x00006208`).

## Observed machine code

JNI_OnLoad contains repeated direct JNIEnv table calls of this form:

```text
729a  ldr    r0, [sp, #0x1cc]       ; JNIEnv
729c  ldr    r1, [r0]               ; native function table
729e  ldr.w  r1, [r1, #0x1c4]      ; native-table byte offset
...
72b4  mov    r1, r3                 ; jclass
72b8  mov    r2, r4                 ; method-name pointer
72ba  mov    r3, r5                 ; signature pointer
...
72c0  blx    r12
```

On ARMv7 JNIEnv table entries are 32-bit words, so
`0x1c4 / 4 = 113`, the standard `GetStaticMethodID` slot. The same offset
is loaded repeatedly later in JNI_OnLoad while caching static method IDs.

## Boundary

This observation establishes GetStaticMethodID slot 113 and its ordinary
JNIEnv/jclass/name/signature calling convention. The binary also contains
direct calls through static invocation slots, but those invocation semantics
remain separate slices.
