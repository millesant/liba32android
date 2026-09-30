# ARM32 JNI raw CallVoidMethod entrypoint evidence — 2026-09-30

## Artifact

The supplied VLC APK:

- `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
- SHA-256 `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

contains `lib/armeabi-v7a/libvlcjni.so`:

- ELF32 little-endian ARM EABI5 shared object for Android 17;
- SHA-256 `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`.

## Observed machine code

Thumb-2 disassembly of `VLCJniObject_attachEvents` shows:

```text
a49c  ldr    r0, [sp, #0x30]       ; JNIEnv
a49e  ldr    r1, [r0]
a4a0  ldr.w  r1, [r1, #0xf4]      ; native table byte offset
...
a4c0  vldr   s0, [sp, #88]
a4c4  vcvt.f64.f32 d16, s0
...
a4d2  vstr   d16, [sp, #16]        ; aligned promoted float argument
a4d6  str.w  r6, [sp, #0xc]
a4da  str.w  r5, [sp, #0x8]
a4de  str.w  r4, [sp, #0x4]
a4e2  str.w  lr, [sp]
a4e8  mov    r1, r2                ; receiver
a4ea  mov    r2, r3                ; jmethodID
a4ec  mov    r3, r12               ; first variadic 32-bit word
a4f2  blx    r12
```

JNIEnv entries are 32-bit words on ARMv7, so `0xf4 / 4 = 61`, the standard
raw variadic `CallVoidMethod` slot. The fixed arguments occupy r0-r2 and the
first variadic core word is forwarded in r3. Additional arguments are staged on
the caller stack. The same call site converts a float source to double before
storing it at an 8-byte-aligned stack location, directly demonstrating default
argument promotion at this boundary.

## Boundary

This observation establishes raw slot 61 and the concrete AAPCS32 variadic
shape used by this VLC call site. It does not establish CallVoidMethodA,
return-valued/static/nonvirtual call families, NewObject, or Java-side method
semantics.
