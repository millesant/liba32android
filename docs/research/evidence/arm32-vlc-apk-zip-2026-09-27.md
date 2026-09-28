# Supplied VLC ARMv7 APK ZIP evidence — 2026-09-27

Artifact inspected locally:

- filename: `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`
- SHA-256: `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`
- file size: 67,261,250 bytes
- ZIP entry count: 2,617
- EOCD: single disk, ordinary ZIP32
- central directory: offset 67,032,336, size 228,892 bytes
- central-directory end equals EOCD offset
- longest entry name observed: 92 bytes
- APK comment length: 0

ARMv7 native entries:

| entry | method | flags | compressed | uncompressed | CRC32 |
| --- | ---: | ---: | ---: | ---: | --- |
| `lib/armeabi-v7a/libc++_shared.so` | 8 (DEFLATE) | 0 | 1,277,471 | 4,169,056 | `25804145` |
| `lib/armeabi-v7a/libmla.so` | 8 (DEFLATE) | 0 | 13,991,697 | 48,233,444 | `402e4bbd` |
| `lib/armeabi-v7a/libvlc.so` | 8 (DEFLATE) | 0 | 21,338,913 | 39,928,540 | `36d0b3d6` |
| `lib/armeabi-v7a/libvlcjni.so` | 8 (DEFLATE) | 0 | 25,545 | 67,112 | `e933ed03` |

All four supplied ARMv7 shared objects therefore require DEFLATE-capable archive
loading. Stored-only ZIP support would not be sufficient for this real target.

The evidence does not imply that every Android APK uses only method 8 or that
ZIP64 can never occur. The implemented source deliberately accepts ordinary
ZIP32 stored + DEFLATE entries and rejects ZIP64 rather than claiming a general
archive implementation.
