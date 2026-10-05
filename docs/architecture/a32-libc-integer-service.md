# A32 libc atoi/strtol host service

Status: accepted current architecture; bounded atoi/strtol compatibility implemented

## Protocol

Feature 035 adds two private service IDs:

- `0xAB` — `atoi`
- `0xAC` — `strtol`

`atoi` consumes the guest string pointer in r0 and returns a signed 32-bit
result in r0.

`strtol` consumes r0 string pointer, r1 optional guest pointer to a 32-bit
end-pointer slot, and signed r2 base. It returns signed 32-bit `long` bits in
r0.

## Errno seam

`A32LibcErrnoSink` is caller-owned. The service calls it only for conversion
conditions where bionic sets errno:

- Android guest EINVAL = 22 for invalid base;
- Android guest ERANGE = 34 for overflow/underflow.

This does not bind to host errno and does not yet expose guest `__errno`.

## Parsing

The parser is bounded by `max_parse_bytes`. It implements ASCII C whitespace,
optional sign, guarded hexadecimal/binary prefixes, base-0 octal/decimal
selection, digits through base 36, no-digit end-pointer semantics, and signed
32-bit saturation.

Overflow continues scanning valid digits to produce the correct end pointer
before returning the signed limit and ERANGE.

## Side effects

Parsing happens in local state. For `strtol`, a non-null guest end-pointer
slot is written only after parsing succeeds. If that write fails, the service
returns `Failed` before changing errno or r0.

## Non-goals

No guest `atoi`/`strtol` ELF exports, `__errno`/TLS storage, locale-aware
ctype, unsigned/wide/64-bit conversion, floating conversion, allocator/thread/
I/O/dynamic-loader behavior, or complete libc compatibility.
