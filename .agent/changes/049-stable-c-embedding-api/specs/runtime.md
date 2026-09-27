# Runtime spec delta — feature 049

Add stable public C embedding API version 1.

Expose an opaque runtime owning logical mapped guest memory, memory
map/protect/unmap/read/write operations, bounded ARM/Thumb execution with
size-tagged request/result structs, exact successful SVC traps, stable statuses,
and optional A32ERR structured text.

Guest addresses remain uint32 logical values. No Dynarmic or C++ type crosses
the public ABI. CMake must publish/install the public header tree.

Do not expose the ELF/linker/compatibility/scheduler/Android I/O composition
objects as public ABI in version 1.
