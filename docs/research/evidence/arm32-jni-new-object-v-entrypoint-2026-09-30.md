# ARM32 JNI NewObjectV entrypoint evidence — 2026-09-30

## Artifact

The supplied VLC APK contains ARMv7 `lib/armeabi-v7a/libmla.so`
(SHA-256 `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`).

Its dynamic symbol table exposes the weak C++ wrapper
`_JNIEnv::NewObject(_jclass*, _jmethodID*, ...)` at Thumb address
`0x0025d9f5`.

## Observed machine code

The wrapper builds the ARM32 `va_list` and then performs:

```text
25da12  ldr    r0, [sp, #0x20]       ; JNIEnv
25da14  add.w  r1, r7, #0x8          ; variadic save area
25da1a  ldr    r1, [r0]              ; JNIEnv function table
25da1c  ldr    r1, [r1, #0x74]       ; native-table byte offset
...
25da32  mov    r1, r2                ; jclass
25da34  mov    r2, r12               ; jmethodID
25da38  mov    r3, lr                ; va_list
25da3e  blx    r12
```

On ARMv7 JNIEnv entries are 32-bit words, so `0x74 / 4 = 29`, the standard
`NewObjectV` slot. The C++ raw-variadic wrapper therefore delegates to
NewObjectV rather than directly loading raw NewObject slot 28.

## Boundary

This observation establishes NewObjectV slot 29 and its JNIEnv/jclass/jmethodID/
va_list calling convention. It does not establish NewObjectA, Java heap layout,
class assignability/inheritance, constructor bytecode execution, or framework
object behavior.
