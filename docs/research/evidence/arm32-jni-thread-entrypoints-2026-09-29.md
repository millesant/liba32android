# ARM32 JNI JavaVM thread entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android package was inspected locally as evidence input;
the APK and extracted binaries are not committed to this repository.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

## AttachCurrentThread

The weak `_JavaVM::AttachCurrentThread(_JNIEnv**, void*)` wrapper loads its
indirect function pointer from byte offset `0x10`:

```text
ldr     r1, [r0]
ldr     r1, [r1, #0x10]
...
blx     r3
```

`0x10 / 4 == 4`, identifying JavaVM invocation-table slot 4.

## DetachCurrentThread

The weak `_JavaVM::DetachCurrentThread()` wrapper loads from byte offset
`0x14`:

```text
ldr     r1, [r0]
ldr     r1, [r1, #0x14]
blx     r1
```

`0x14 / 4 == 5`, identifying slot 5.

## Existing GetEnv cross-check

The same supplied binary loads JavaVM::GetEnv from byte offset `0x18`, slot 6,
matching the already accepted bootstrap.

## Boundary

This evidence establishes table positions and real binary demand. It does not by
itself require a general host-thread registry, JavaVMAttachArgs interpretation,
AttachCurrentThreadAsDaemon, or Java thread objects.
