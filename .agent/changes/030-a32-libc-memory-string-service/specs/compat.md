# Compatibility spec delta — feature 030

Add one bounded A32 host-service handler for memcpy, memset, memcmp, memchr,
strlen, strcmp, and strncmp, using shared private SVC IDs 0xA1-0xA7.

The handler consumes ordinary AAPCS32 arguments from r0-r2 and returns logical
guest pointers, 32-bit lengths, or signed comparison results through r0.
Unknown IDs remain Unhandled.

The caller supplies finite memory-transfer and string ceilings. Counts above
their relevant ceiling, 32-bit guest-range wrap, or guest-memory access failure
return Failed. Zero-count operations access no guest memory. strlen accepts a
payload exactly at the string ceiling followed by NUL; strncmp reads at most
its explicit count.

Do not add a guest libc.so shim/provider, allocation/thread/I/O/dynamic-loader
state, errno, libm, or broader libc behavior in this change.
