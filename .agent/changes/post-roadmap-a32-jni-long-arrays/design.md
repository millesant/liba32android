# Design — ARM32 JNI jlong arrays

## Evidence-backed entries

Direct disassembly of the supplied ARMv7 `libmla.so` identifies:

- NewLongArray at JNIEnv offset `0x2d0`, slot 180;
- GetLongArrayElements at `0x2f0`, slot 188;
- ReleaseLongArrayElements at `0x310`, slot 196;
- SetLongArrayRegion at `0x350`, slot 212.

The SetLongArrayRegion wrapper passes its fifth C argument from the ARM32 stack,
so the host service reads the guest buffer pointer from `[sp]`.

## Storage and handles

The bounded JNI registry allocates synthetic logical array handles from a
caller-configurable base/stride range. Allocation skips every handle already in
the generic reference ledger. A new long array owns a finite
`std::vector<int64_t>`, is zero-initialized, is registered in the generic
array-length metadata, and starts with one local reference.

Per-array element count has an explicit hard-capped limit. Host pointers are
never published as jarray or jlong* values.

## Element scratch lease

The VM layout contains one caller-owned, 8-byte-aligned guest scratch region.
GetLongArrayElements copies the full owned array into that region, optionally
writes JNI_TRUE to `isCopy`, returns the logical scratch address, and records
one active long-array lease.

ReleaseLongArrayElements accepts the exact leased array/scratch pair:

- mode 0: copy scratch back into owned storage and release;
- JNI_COMMIT (1): copy back and keep the lease active;
- JNI_ABORT (2): discard scratch changes and release.

Any other mode, wrong pointer, wrong array, or overlapping lease fails.

## SetLongArrayRegion

The service treats start/length as signed jsize values, rejects negative or
out-of-bounds ranges, obtains the guest source pointer from the fifth ARM32
argument at `[sp]`, decodes little-endian jlong values, and copies them into
owned array storage. A zero-length region performs no guest-buffer read.

## Boundary

Other primitive array families, object arrays, Get/SetLongArrayRegion variants
not evidenced here, GetPrimitiveArrayCritical, pinning, and multiple concurrent
leases remain separate.
