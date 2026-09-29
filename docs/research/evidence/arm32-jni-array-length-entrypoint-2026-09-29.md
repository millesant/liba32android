# ARM32 JNI GetArrayLength entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android package was inspected locally as evidence input;
the APK and extracted binaries are not committed to this repository.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

`_JNIEnv::GetArrayLength(_jarray*)` loads its indirect function pointer from
JNIEnv byte offset `0x2ac`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x2ac]
...
blx     r2
```

`0x2ac / 4 == 171`, identifying JNIEnv slot 171.

The same supplied binary also exposes NewLongArray, NewObjectArray, object-array
element access, and long-array region/element APIs. Those broader semantics are
not inferred into this small GetArrayLength slice.
