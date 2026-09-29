# ARM32 JNI ThrowNew entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android APK was inspected locally as evidence input. The
APK and extracted binary are not committed.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

The ARMv7 weak `_JNIEnv::ThrowNew` wrapper loads its JNIEnv function pointer
from byte offset `0x38` before the indirect call:

```text
ldr     r1, [r0]
ldr     r1, [r1, #0x38]
...
blx     r3
```

`0x38 / 4 == 14`, identifying JNIEnv slot 14. JNIEnv, jclass, and message
pointer are forwarded in r0-r2 and the jint status returns in r0.

## Boundary

This evidence establishes the table position and call ABI. It does not establish
Throwable object representation, stack traces, Java unwinding, or the rest of
the JNI exception API family.
