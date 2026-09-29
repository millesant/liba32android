# ARM32 JNI object-array entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android APK was inspected locally as evidence input. The
APK and extracted binary are not committed.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

Direct ARMv7 wrappers load the following JNIEnv entries:

| JNI call | byte offset | slot |
| --- | ---: | ---: |
| NewObjectArray | `0x2b0` | 172 |
| GetObjectArrayElement | `0x2b4` | 173 |
| SetObjectArrayElement | `0x2b8` | 174 |

The wrappers pass length/class/initial-element or array/index/value in the normal
ARM32 r1-r3 argument registers; no extra stack argument is needed.

## Boundary

The observed wrappers prove object-array demand but do not establish a Java
inheritance graph, class assignability checks, ArrayStoreException, object
construction, or general garbage collection.
