# ARM32 JNI member-ID entrypoint evidence — 2026-09-28

## Supplied artifact

The user-supplied VLC Android package was inspected locally as an evidence input;
the APK and extracted binaries are not committed to this repository.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

The extracted library is ARM EABI5 and contains weak C++ JNIEnv wrappers whose
machine code identifies additional native-interface slots.

## GetMethodID

`_JNIEnv::GetMethodID(_jclass*, char const*, char const*)` loads its indirect
function pointer from byte offset `0x84`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x84]
...
blx     r12
```

`0x84 / 4 == 33`, identifying JNIEnv slot 33.

## GetFieldID

`_JNIEnv::GetFieldID(_jclass*, char const*, char const*)` loads its function
pointer from byte offset `0x178`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x178]
...
blx     r12
```

`0x178 / 4 == 94`, identifying JNIEnv slot 94.

## GetStaticFieldID

`_JNIEnv::GetStaticFieldID(_jclass*, char const*, char const*)` loads its
function pointer from byte offset `0x240`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x240]
...
blx     r12
```

`0x240 / 4 == 144`, identifying JNIEnv slot 144.

## Boundary

The targeted symbol/disassembly pass did not establish equivalent direct wrapper
evidence for GetStaticMethodID, GetObjectClass, or IsInstanceOf. Those APIs are
therefore intentionally excluded from this bounded slice rather than inferred
from the general JNI table.
