# Design — additional bounded libc copy/search services

## memmem

Inputs are r0 haystack, r1 haystack length, r2 needle, r3 needle length.
Lengths must each be <= max_transfer_bytes and logical ranges must not wrap.

needle length zero returns r0 unchanged without memory access. A shorter
haystack than non-empty needle returns null without memory access. Otherwise
copy both finite ranges through GuestMemory and search the owned byte arrays.

## strcpy

Read source one byte at a time through GuestMemory into temporary storage,
including its terminating NUL. Accept exactly max_string_bytes payload bytes
when the following byte is NUL. Require the complete byte count including NUL
to fit max_transfer_bytes and destination 32-bit range before one destination
write.

## strncpy

Count is r2 and must fit max_transfer_bytes. Zero count returns destination
without reading either pointer. For non-zero count, allocate an exact count-byte
temporary result. Read source until count or NUL; after NUL leave the remaining
temporary bytes zero. Write exactly count bytes once.

## Failure atomicity

strcpy/strncpy complete all required source reads before destination write, so a
source fault cannot partially mutate destination.

## Verification

Focused direct tests cover exact guest pointer results, memmem empty/short
fast-paths, strcpy source-bound failure without destination mutation, strncpy
padding and non-terminated truncation, zero-count semantics, and wrap failure.
