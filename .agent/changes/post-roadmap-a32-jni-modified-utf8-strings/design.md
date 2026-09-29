# Design — ARM32 JNI modified-UTF-8 strings

## Evidence-backed JNIEnv entries

Supplied ARMv7 libmla.so machine code loads:

- NewStringUTF from byte offset 0x29c, slot 167;
- GetStringUTFChars from byte offset 0x2a4, slot 169;
- ReleaseStringUTFChars from byte offset 0x2a8, slot 170.

The existing 216-word JNIEnv table already spans those positions.

## Registry model

The bounded JNI registry owns finite string metadata. New strings receive
logical 32-bit handles from a caller-configurable base/stride range. Allocation
skips handles already known to the generic reference ledger, retains one local
reference, and copies the input bytes into host-owned storage. No host pointer
is exposed.

The registry bounds total strings and modified-UTF-8 payload bytes. This slice
preserves input bytes exactly; it assumes callers supply JNI-compatible
modified-UTF-8 and does not yet normalize or fully validate Unicode encodings.

## UTF chars lease

The caller supplies one already-mapped writable guest scratch region in the VM
layout. GetStringUTFChars supports one outstanding lease at a time. For a known
live string it copies payload bytes plus NUL into that region, writes JNI_TRUE
to isCopy when non-null, returns the logical scratch address, and records the
leased string handle.

ReleaseStringUTFChars requires the exact leased string handle and exact scratch
pointer, then clears the lease. It does not expose or retain host string
storage.

## Deliberate boundary

This slice does not implement UTF-16 APIs, GetStringUTFLength, region APIs,
multiple simultaneous char leases, pinning semantics, Unicode normalization,
or full modified-UTF-8 validation.
