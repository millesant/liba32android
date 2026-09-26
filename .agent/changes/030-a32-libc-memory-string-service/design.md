# Design — bounded A32 libc memory/string service

## Shared IDs

The compatibility header defines:

- memcpy 0xA1
- memset 0xA2
- memcmp 0xA3
- memchr 0xA4
- strlen 0xA5
- strcmp 0xA6
- strncmp 0xA7

These are private guest/host protocol values, not Linux syscall numbers or
Android API identifiers.

## Handler

A32LibcMemoryStringService implements A32HostServiceHandler and stores only two
ceilings: max_transfer_bytes and max_string_bytes.

Unknown IDs return Unhandled. The service does not modify CPSR.

## Transfer operations

memcpy/memset/memcmp/memchr consume r0/r1/r2. Count above
max_transfer_bytes or a logical 32-bit range that wraps returns Failed.

memcpy copies the complete source range into bounded host temporary storage
before destination write. memset writes the low eight bits of r1. memcmp
compares unsigned bytes and returns -1/0/1. memchr returns the logical guest
address of the first matching byte or zero.

Zero-count calls perform no guest-memory access.

## String operations

strlen scans from r0 through GuestMemory and permits exactly max_string_bytes
payload bytes followed by NUL.

strcmp scans r0/r1 in lockstep. A byte difference returns immediately; equal
bytes continue until NUL or the string ceiling.

strncmp uses r2 as the maximum compared count, rejects counts above the string
ceiling, reads at most that many byte pairs, and does not require a NUL within
the explicit count.

Comparison results are written bit-for-bit as signed 32-bit -1/0/1.

## Verification

Direct deterministic tests cover all operations, bounds, read failures, logical
address wrap, unknown service IDs, and sign/pointer results. An ARM
svc #0xa1; bx lr program proves memcpy through feature-025 registry and
feature-024 resumable dispatch.

No guest libc ELF or platform provider is part of this change.
