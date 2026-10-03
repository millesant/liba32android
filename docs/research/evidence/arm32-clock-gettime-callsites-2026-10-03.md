# ARM32 clock_gettime call-site evidence — 2026-10-03

## Input

The inspected artifact is the supplied
`VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk`, SHA-256
`10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`.

The relevant ARMv7 entries are
`lib/armeabi-v7a/libc++_shared.so` and
`lib/armeabi-v7a/libvlc.so`. Both contain eager
`R_ARM_JUMP_SLOT clock_gettime@LIBC` relocations.

## Direct call-site evidence

Bounded `llvm-objdump -d --no-show-raw-insn` inspection resolves the
clock_gettime PLT entry in each DSO and records the immediate value placed in
r0 before the call.

`libc++_shared.so`:

- 0x0003ffc2: r0 = 0;
- 0x00040050: r0 = 1.

`libvlc.so`:

- 0x002a469c: r0 = 1;
- 0x00652fd4: r0 = 0;
- 0x00c98e1c: r0 = 1;
- 0x00c98e98: r0 = 0;
- 0x00c98ffc: r0 = 1;
- 0x00cea2e4: r0 = 0;
- 0x00e99abc: r0 = 0;
- 0x01977664: r0 = 4;
- 0x019777f8: r0 = 4.

For the Android/Linux clockid ABI these values are
`CLOCK_REALTIME=0`, `CLOCK_MONOTONIC=1`, and
`CLOCK_MONOTONIC_RAW=4`.

No other clock ID is required by the inspected direct call sites. This evidence
therefore justifies exactly those three IDs for the next bounded compatibility
slice; it does not justify broader clock/timer APIs.
