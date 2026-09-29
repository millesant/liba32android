# ARM32 JNI CallVoidMethodV entrypoint evidence — 2026-09-29

## Artifact

User-supplied APK:

- file: `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
- SHA-256: `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

Observed library inside the APK:

- entry: `lib/armeabi-v7a/libmla.so`
- SHA-256: `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`
- ELF: 32-bit little-endian ARM EABI5, Android 17, NDK r21e
- build ID: `2b7630a2c45d6bc9e1400f0964b4a3bbfcb90ade`

## Direct wrapper evidence

The library contains the weak function
`_JNIEnv::CallVoidMethod(_jobject*, _jmethodID*, ...)` at `0x269474`.

Relevant instructions:

```text
26947c  str     r3, [r7, #0x8]       ; save first variadic register word
269494  add.w   r1, r7, #0x8
269498  str     r1, [sp, #0x1c]      ; va_list cursor
26949a  ldr     r1, [r0]             ; JNIEnv function table
26949c  ldr.w   r1, [r1, #0xf8]      ; slot 62
...
2694b4  mov     r1, r2               ; jobject
2694b6  mov     r2, r12              ; jmethodID
2694ba  mov     r3, lr               ; va_list
2694c0  blx     r12
```

JNI native-table offset `0xf8 / 4 = 62`, which is CallVoidMethodV in the
Android JNI layout. The wrapper therefore proves that a normal C++ variadic
CallVoidMethod invocation in this ARMv7 library is normalized to the V entry
before crossing the JNIEnv function table.

The first three fixed call operands are JNIEnv, jobject, and jmethodID. The
fourth operand to slot 62 is the guest ARM32 `va_list` pointer.

## ABI implication

The service for slot 62 must interpret an ARM32 variadic cursor, not treat r3 as
the first Java argument unconditionally. AAPCS32 variable arguments require
8-byte alignment before consuming 64-bit values. C default argument promotions
also mean a Java `float` supplied to the original variadic wrapper is present
in the va_list as `double` and must be narrowed back to jfloat semantics.

## Scope boundary

This evidence directly supports CallVoidMethodV slot 62 only. It does not by
itself justify publishing raw variadic CallVoidMethod slot 61, CallVoidMethodA,
return-valued/static/nonvirtual call families, NewObject, receiver class
inference, inheritance dispatch, or Java framework method behavior.
