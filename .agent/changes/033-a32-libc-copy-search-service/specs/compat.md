# Compatibility spec delta — feature 033

Extend A32LibcMemoryStringService with memmem/strcpy/strncpy at private shared
SVC IDs 0xA8/0xA9/0xAA.

memmem uses r0-r3 and bounds both explicit byte ranges. Empty needle returns the
haystack pointer without reads; haystack shorter than non-empty needle returns
null without reads; otherwise return first exact logical guest match.

strcpy must observe a source NUL within max_string_bytes, buffer the complete
source including NUL before mutation, require the full copy to fit the transfer
ceiling and destination range, then write once.

strncpy bounds count by max_transfer_bytes; zero count accesses no guest memory;
non-zero copies at most count source bytes, pads after NUL, and copies exactly
count bytes without inventing NUL if the source is unterminated within count.

No parsing/errno/allocator/thread/I/O/dynamic-loader/math or shim-export changes
are part of feature 033.
