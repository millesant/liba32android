# ARM32 libc clock_gettime compatibility

Status: accepted current architecture; bounded clock_gettime compatibility implemented

## Goal

Expose the guest-visible ARM32 `clock_gettime` calls directly exercised by
the supplied VLC binaries without coupling compatibility behavior to host wall
time or widening the pthread scheduler model.

## Evidence boundary

Direct disassembly of the supplied VLC ARMv7 artifacts finds
`clock_gettime@LIBC` call sites using:

- `CLOCK_REALTIME = 0`;
- `CLOCK_MONOTONIC = 1`;
- `CLOCK_MONOTONIC_RAW = 4`.

The supplied `libc++_shared.so` uses IDs 0 and 1. The supplied
`libvlc.so` uses IDs 0, 1, and 4. No other clock ID is selected for this
slice.

See
[ARM32 clock_gettime call-site evidence](../research/evidence/arm32-clock-gettime-callsites-2026-10-03.md).

## Logical clock source

`A32LibcClockService` borrows the existing `A32PthreadClock` abstraction
already used by deterministic condition-variable timed waits. The name is
historical; the object is embedding-owned logical time and no compatibility
service calls host `clock_gettime` or another host wall-clock function.

`A32PthreadClockId` now has Realtime=0, Monotonic=1, and MonotonicRaw=4.
Condition-variable timed waits continue to request only Realtime. The libc
service selects any of the three evidenced IDs and asks the same source for
nanoseconds.

## Guest ABI and errors

The partial guest `libc.so` exports `clock_gettime` as private SVC
`0x132`.

r0 carries the signed ARM32 clock ID and r1 carries a logical guest pointer to
an LP32 timespec. Successful output is exactly eight little-endian bytes:

```text
int32_t tv_sec
int32_t tv_nsec
```

Logical nanoseconds are normalized so tv_nsec is always
0..999,999,999. Signed seconds must fit the LP32 time_t range.

Unsupported or unavailable clock IDs return -1 and guest EINVAL. Invalid guest
output pointers return -1 and guest EFAULT. LP32 seconds overflow returns -1
and guest EOVERFLOW. The service does not clear errno on success.

## Integration

The reproducible base ARM32 libc consumer imports `clock_gettime` through an
ordinary eager JUMP_SLOT. Integration injects deterministic values for all
three evidenced IDs, executes the real consumer wrapper through the partial
libc SVC stub, and verifies the two guest words.

This remains a bounded compatibility seam, not a time subsystem. CPU clocks,
coarse/boottime/alarm/dynamic IDs, time64, setting clocks, sleeps, timers, and
host passthrough remain outside this slice.
