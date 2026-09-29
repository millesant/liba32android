# ARM32 JNI reference entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android package was inspected locally as evidence input;
the APK and extracted binaries are not committed to this repository.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

The library is a 32-bit ARM EABI5 Android shared object built by NDK r21e.

## NewGlobalRef

`_JNIEnv::NewGlobalRef(_jobject*)` loads its function pointer from byte offset
`0x54`:

```text
ldr     r1, [r0]
ldr     r1, [r1, #0x54]
...
blx     r2
```

`0x54 / 4 == 21`, identifying JNIEnv slot 21.

## DeleteGlobalRef

`_JNIEnv::DeleteGlobalRef(_jobject*)` loads from byte offset `0x58`, so
`0x58 / 4 == 22`.

## DeleteLocalRef

`_JNIEnv::DeleteLocalRef(_jobject*)` loads from byte offset `0x5c`, so
`0x5c / 4 == 23`.

## Weak-reference boundary

The same binary contains `NewWeakGlobalRef`, loading JNIEnv offset `0x388`
(slot 226), but the targeted weak-symbol pass did not find
`DeleteWeakGlobalRef`. The strong/local slice therefore does not publish weak
reference APIs yet.
