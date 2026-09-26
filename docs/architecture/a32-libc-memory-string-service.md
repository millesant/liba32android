# A32 libc memory/string host service

Status: feature 030 implementation prepared OFF-REF; exact-head validation NOT RUN

## Motivation

Static evidence from the supplied ARM32 FMOD library and VLC ARMv7
`libvlc.so` shows both import the same seven bounded libc primitives:
`memcpy`, `memset`, `memcmp`, `memchr`, `strlen`, `strcmp`, and
`strncmp`.

Feature 030 implements only the host-service side, mirroring the earlier
feature-026/027 split for Android logging. A guest `libc.so` shim/provider is
separate work.

## Private service IDs

`src/compat/a32_libc_memory_string.h` defines the guest/host protocol:

- `0xA1` — `memcpy`
- `0xA2` — `memset`
- `0xA3` — `memcmp`
- `0xA4` — `memchr`
- `0xA5` — `strlen`
- `0xA6` — `strcmp`
- `0xA7` — `strncmp`

The header is preprocessor-safe so a future ARM32 guest shim can include the
same definitions instead of duplicating service numbers.

## ABI mapping

The service consumes ordinary AAPCS32 machine-word arguments directly from
r0-r2 and writes the function result to r0.

Pointer results remain logical guest addresses. Signed `int` comparison
results are written bit-for-bit to r0 and use only the portable sign contract:
negative, zero, or positive.

## Bounds and failures

The caller configures two finite ceilings:

- `max_transfer_bytes` for memory operations;
- `max_string_bytes` for string scanning and `strncmp` count.

Transfer operations reject counts above their ceiling and logical 32-bit ranges
that wrap. Read/write faults return `Failed`.

`strlen` accepts a payload exactly `max_string_bytes` long when the next
byte is NUL. `strcmp` may return as soon as a differing byte determines the
result; otherwise equal unterminated input at the ceiling fails. `strncmp`
reads at most its explicit count and does not require a terminator.

Zero-count `memcpy`, `memset`, `memcmp`, `memchr`, and `strncmp`
perform no guest memory access and return their ordinary zero-count result.

For `memcpy`, the complete bounded source range is copied into host temporary
storage before the destination write is attempted, so a source read failure
cannot mutate the destination.

## Deliberate exclusions

No guest `libc.so` DSO/provider, allocator, `strcpy`/`strncpy`,
`strtol`, stdio, file/socket I/O, pthread state, dynamic-loader APIs, libm
functions, errno semantics, or Android namespace/provider installation is added
by feature 030.
