# ARM32 JNI modified-UTF-8 entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android package was inspected locally as evidence input;
the APK and extracted binaries are not committed to this repository.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

## NewStringUTF

The weak JNIEnv wrapper loads its indirect function pointer from byte offset
`0x29c`. `0x29c / 4 == 167`, identifying JNIEnv slot 167.

## GetStringUTFChars

The corresponding wrapper loads byte offset `0x2a4`.
`0x2a4 / 4 == 169`, identifying slot 169.

## ReleaseStringUTFChars

The release wrapper loads byte offset `0x2a8`.
`0x2a8 / 4 == 170`, identifying slot 170.

## Boundary

These observations prove real binary demand and exact table positions. They do
not by themselves require UTF-16 APIs, GetStringUTFLength, region APIs,
multiple simultaneous char leases, or a complete Java String implementation.
