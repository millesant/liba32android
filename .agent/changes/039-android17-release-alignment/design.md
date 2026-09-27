# Design — Android 17 release alignment

## Source policy

Current Android semantic contracts resolve `android-latest-release` first,
then cite the selected release branch/tag. Historical records retain the
revision they actually used.

## memmem ordering

Keep caller-selected length ceilings as the first safety check. Then apply the
observable no-read cases:

1. empty needle -> return haystack;
2. haystack length < non-empty needle length -> return null.

Only a search that can inspect bytes validates both guest ranges and performs
GuestMemory reads.

## strncpy ordering

Validate count ceiling and the full destination range first. Zero count returns
without access.

For non-zero count, read source one byte at a time. Stop source access at NUL,
leave the remaining temporary output zero-padded, then perform one destination
write. Do not validate a nominal source count range that the function never
reads.

## Regression boundary

A memmem regression uses a nominally wrapping haystack with empty needle and
requires a no-read return.

A sparse-memory strncpy regression places `{'A', 0}` at `0xfffffffe` and
requests count four into a valid destination. Success proves padding does not
invent source accesses past the 32-bit guest address space.

## Other audited contracts

Direct namespace link accessibility, atoi/strtol, guest errno pointer behavior,
and Android log-write ABI require provenance updates but no implementation
change within their accepted/prepared boundaries.
