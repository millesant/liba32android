# ARM32 JNI instance-long-field entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android APK was inspected locally as evidence input. The
APK and extracted binary are not committed.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

## GetLongField

The ARMv7 weak wrapper loads its JNIEnv entry from byte offset `0x194`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x194]
...
blx     r3
```

`0x194 / 4 == 101`, identifying JNIEnv slot 101. The jlong return follows the
AAPCS32 64-bit result convention in r0/r1.

## SetLongField

The wrapper loads byte offset `0x1b8`:

```text
ldr     r1, [r0]
ldr.w   r1, [r1, #0x1b8]
...
str.w   high_word, [sp, #4]
str.w   low_word, [sp]
blx     r3
```

`0x1b8 / 4 == 110`, identifying slot 110. Because r0-r2 are occupied by
JNIEnv, jobject, and jfieldID, the aligned jlong argument is passed through the
guest stack as low/high words at `[sp]` and `[sp+4]`.

## Boundary

The evidence proves the two table positions and ARM32 value ABI. It does not
establish Java object layout, field offsets, object/class assignability,
inheritance, volatile semantics, or the rest of the field-access families.
