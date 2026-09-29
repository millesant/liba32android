# ARM32 JNI jlong-array entrypoint evidence — 2026-09-29

## Supplied artifact

The user-supplied VLC Android APK was inspected locally as evidence input. The
APK and extracted binary are not committed.

| artifact | SHA-256 |
| --- | --- |
| VLC Android APK | `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5` |
| `lib/armeabi-v7a/libmla.so` | `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962` |

## Observed JNIEnv table loads

The weak ARMv7 wrappers load these function pointers before indirect calls:

| JNI call | table byte offset | slot |
| --- | ---: | ---: |
| NewLongArray | `0x2d0` | 180 |
| GetLongArrayElements | `0x2f0` | 188 |
| ReleaseLongArrayElements | `0x310` | 196 |
| SetLongArrayRegion | `0x350` | 212 |

For SetLongArrayRegion the ARM32 wrapper retrieves the fifth C argument from its
incoming stack frame before making the indirect JNI call. This confirms that the
guest service must decode the source buffer pointer through the guest stack
rather than inventing an extra register argument.

## Boundary

This evidence establishes demand for the jlong family above. It does not by
itself justify every primitive-array type, object arrays, critical-array APIs,
pinning semantics, or multiple simultaneous element leases.
