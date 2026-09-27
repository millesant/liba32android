# Design — stable C embedding API version 1

## Public header

Install `include/liba32android/liba32android.h`.

The header is valid C and C++. It contains:
- API version constant/function;
- stable status enum and status-name helper;
- opaque runtime handle;
- memory permission flags;
- size-tagged execution request/result structs;
- caller-owned structured-error buffer;
- runtime memory/execution functions.

## Runtime object

The opaque implementation contains one `MappedGuestMemory`. Creation may fail
while reserving the logical 4 GiB AArch32 address space; failures are converted
to public status/error text and never throw across the C ABI.

## Execution translation

Copy public request bits into the existing engine-independent
`cpu::ExecutionRequest`. Reject zero/overflowing budget, unknown request flags,
unknown ISA, or undersized structs.

Copy the internal execution snapshot back before interpreting terminal state.
SVC with exact immediate is a successful trap. Memory fault is MEMORY_ERROR.
A non-SVC exception is EXECUTION_ERROR. Ordinary budget/stop completion is OK.

## Structured errors

The library formats only stable internal component/code/message literals into a
fixed local buffer, then copies/truncates into caller storage. Error formatting
does not allocate. Required length excludes NUL. Success clears the caller
buffer.

## ABI evolution

`struct_size` is a capacity/version seam. Version 1 accepts current-or-larger
request/result objects and writes only the known prefix. New fields can be
appended in future API versions.

## Packaging/test

Make the public include directory a target usage requirement and install it with
the shared library.

Compile the regression as C, not C++, so accidental private/C++ dependencies are
caught immediately.
