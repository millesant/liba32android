# ARM32 JNI GetIntField entrypoint evidence — 2026-09-30

## Artifact

The supplied VLC APK:

- `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
- SHA-256 `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

contains `lib/armeabi-v7a/libvlcjni.so`:

- ELF32 little-endian ARM EABI5 shared object for Android 17;
- SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`.

The dynamic symbol table records
`Java_org_videolan_libvlc_Media_nativeNewFromFd` at Thumb address
`0x0000a885` (code address `0x0000a884`).

## Observed machine code

The export contains:

```text
a8e6  ldr    r0, [sp, #0x10]       ; JNIEnv
a8e8  ldr    r1, [r0]
a8ea  ldr.w  r1, [r1, #0x190]      ; native table byte offset
a8ee  ldr    r2, [sp, #0xc]        ; jobject
...
a8f6  ldr    r3, [r3, #0x1c]       ; cached jfieldID
...
a8fa  mov    r1, r2
a8fc  mov    r2, r3
a900  blx    r3
a902  str    r0, [sp, #0x8]        ; jint result
```

JNIEnv entries are 32-bit words on ARMv7, so `0x190 / 4 = 100`, the standard
`GetIntField` slot. The call forwards JNIEnv, jobject, and jfieldID in r0-r2
and receives the jint value in r0.

The same function subsequently loads JNIEnv offsets `0x3c` (slot 15,
ExceptionOccurred) and `0x44` (slot 17, ExceptionClear). Those are useful
adjacent evidence but are intentionally not inferred into this field slice.

## Boundary

This observation establishes GetIntField slot 100 and its ordinary ARM32 JNI
argument/return registers. It does not establish SetIntField, Java field
layout, object-class assignability, inheritance, volatile semantics, or the
behavior of the adjacent exception APIs.
