# Design — ARM32 JNI byte-array element leases

## Evidence boundary

Direct disassembly of supplied ARMv7 `libfmod.so` shows exported
`Java_org_fmod_MediaCodec_fmodReadAt` loading JNIEnv native-table offsets
`0x2e0` and `0x300`. Dividing by the 32-bit table word size identifies
slots 184 and 192: `GetByteArrayElements` and
`ReleaseByteArrayElements`.

No machine-code evidence in this slice is used to infer `NewByteArray`,
byte-region APIs, or other primitive-array families.

## Storage model

The caller seeds a logical `jbyteArray` handle with owned bounded bytes.
The existing JNI reference registry owns liveness counts, and the generic array
registry owns the length metadata used by `GetArrayLength`.

Byte data remains host-owned. Guest code only sees the logical array handle and
a bounded guest scratch copy during an active element lease.

## Lease behavior

`GetByteArrayElements` validates exact JNIEnv/attached state, a live seeded
byte array, available scratch capacity, and no overlapping byte-array lease.
It copies the owned bytes into guest scratch, writes JNI_TRUE when `isCopy` is
non-null, and returns the scratch guest pointer.

`ReleaseByteArrayElements` requires the exact active array and scratch pointer.
Mode 0 copies back and closes the lease, JNI_COMMIT copies back and keeps the
lease active, and JNI_ABORT discards guest changes and closes the lease.

## Boundary

This slice does not publish host pointers and does not introduce Java object
layout, framework dispatch, NewByteArray, byte-region calls, or broader
primitive-array support.
