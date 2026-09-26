# Additional A32 libc copy/search services

Status: feature 033 implementation prepared OFF-REF; exact-head validation NOT RUN

Feature 033 extends `A32LibcMemoryStringService` with three additional
primitives present in both supplied ARM32 targets:

- `memmem` — SVC `0xA8`
- `strcpy` — SVC `0xA9`
- `strncpy` — SVC `0xAA`

## memmem

AAPCS32 supplies haystack pointer/length in r0/r1 and needle pointer/length in
r2/r3. Both lengths are bounded by `max_transfer_bytes`; logical ranges must
not wrap.

An empty needle returns the haystack pointer without guest reads. A haystack
shorter than the non-empty needle returns null without guest reads. Otherwise
both complete ranges are copied through GuestMemory and the first byte-exact
match returns a logical guest pointer.

## strcpy

The source is copied through GuestMemory into temporary storage, including its
terminating NUL. The payload may contain exactly `max_string_bytes` non-NUL
bytes followed by NUL. The complete copy including NUL must fit
`max_transfer_bytes` and the destination logical range before any destination
write occurs.

## strncpy

The explicit count is bounded by `max_transfer_bytes`. Zero count accesses no
guest memory and returns the original destination pointer.

For non-zero count, source bytes are read only until NUL or count. After an
observed NUL, the temporary result is padded with NUL bytes to exactly count.
If no NUL appears in the first count source bytes, exactly count bytes are
copied and no terminator is invented.

## Non-goals

No integer parsing, locale, errno, allocation, thread, I/O, dynamic-loader, or
guest-shim export changes are included. A later guest libc shim extension must
use the same shared service-ID header.
