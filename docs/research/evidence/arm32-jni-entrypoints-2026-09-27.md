# ARM32 JNI entrypoint evidence — 2026-09-27

## Android/Dalvik ABI references

Android's JNI invocation table places `GetEnv` after the three reserved slots,
`DestroyJavaVM`, `AttachCurrentThread`, and `DetachCurrentThread`. On a
32-bit guest this is table slot 6, byte offset `0x18`.

Reference:
`https://android.googlesource.com/platform/libnativehelper/+/ad83f63/include/nativehelper/jni.h`

Dalvik's `GetEnv` implementation accepts the inclusive numeric range from
`JNI_VERSION_1_1` (`0x00010001`) through
`JNI_VERSION_1_6` (`0x00010006`). A version outside that range returns
`JNI_EVERSION` before touching `*env`. The current LibA32Android bootstrap
models one already-attached execution context, so an in-range request returns
the configured guest `JNIEnv*`.

Reference:
`https://android.googlesource.com/platform/dalvik/+/bbf31b58c50fb892423b7fef0d8c1093bd0c1a6c/vm/Jni.cpp`

Dalvik load-time `JNI_OnLoad` accepts only exact returns
`JNI_VERSION_1_2`, `JNI_VERSION_1_4`, or `JNI_VERSION_1_6`; other returns
mark the native load failed.

Reference:
`https://android.googlesource.com/platform/dalvik2/+/master/vm/Native.cpp`

## Supplied ARMv7 binaries

The following user-supplied or APK-extracted binaries were inspected locally.
They are evidence inputs only and are not checked into the repository.

| artifact | SHA-256 | observed JNI entrypoints |
| --- | --- | --- |
| `libvlc.so` | `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4` | `JNI_OnLoad` at `0x002a2ee4` (1044 bytes), `JNI_OnUnload` |
| `libvlcjni.so` | `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816` | `JNI_OnLoad` at `0x00006209` (6456 bytes), `JNI_OnUnload` |
| `libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` | `JNI_OnLoad` at `0x0025f361` (8264 bytes), `JNI_OnUnload`, weak JavaVM/JNIEnv C++ wrappers |
| `libfmod.so` | `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544` | `JNI_OnLoad` at `0x000ccb68` (332 bytes) |
| `libemu32.so` | `a467c34bc1543a2a193191ad42c4ac3a8e4a00181e83fa223abf7d42bc421119` | ELF64/AArch64, outside the ARM32 JNI slice |

### libmla.so JavaVM::GetEnv wrapper

The ARM/Thumb disassembly of weak
`_JavaVM::GetEnv(void**, int)` loads the function table from `[r0]`, then
loads the indirect function pointer from byte offset `0x18`, and calls it with
`blx`.

Relevant instructions:

```text
ldr   r1, [r0]
ldr   r1, [r1, #0x18]
...
blx   r3
```

This is direct machine-code evidence for the ARM32 JavaVM `GetEnv` table slot
used by the first compatibility slice.

### libmla.so JNIEnv::RegisterNatives wrapper

The weak
`_JNIEnv::RegisterNatives(_jclass*, JNINativeMethod const*, int)` wrapper
loads its function pointer from byte offset `0x35c` of the JNIEnv native
table before an indirect call.

Relevant instructions:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x35c]
...
blx     r12
```

`0x35c / 4 == 215`, so this real binary identifies JNI native-table slot 215
as an evidence-backed next compatibility target. The current JNI_OnLoad/GetEnv
slice intentionally does not publish that slot yet.

## Generated fixture cross-check

The deterministic ARM32 JNI fixture is designed to export only
`JNI_OnLoad`, have no `DT_NEEDED` dependencies, and call
`vm->functions->GetEnv` indirectly. A local cross-compile sanity check
confirmed an ELF32 ARM shared object with no relocations/DT_NEEDED and generated
code that loads the JavaVM table followed by the function pointer at
`[table + 0x18]` before `blx`.

The pinned-NDK CI lane remains the acceptance authority for the repository
fixture.
