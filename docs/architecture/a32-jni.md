# ARM32 JNI compatibility

Status: JNI VM / GetEnv / JNI_OnLoad bootstrap implemented; exact-head validation pending

## Goal

Provide the smallest guest-visible JNI invocation ABI needed to enter real
ARM32 Android native libraries through `JNI_OnLoad`, while keeping Java
objects/classes and native-method registration outside this first slice.

The design reuses existing logical guest memory, ELF symbol lookup, A32 service
dispatch, and lifecycle execution-context machinery. It does not introduce a
host JVM or publish host pointers.

## Guest JavaVM and JNIEnv

`A32JniVmService` installs a caller-addressed JavaVM object, JNI invocation
table, JNIEnv object, a minimal native-interface prefix, and an ARM GetEnv SVC
stub.

On ARM32 the JavaVM table entry for GetEnv is slot 6 / byte offset `0x18`.
Only that invoke entry is non-null. The supplied `libmla.so` independently
confirms the ABI in machine code: its `_JavaVM::GetEnv(void**, int)` wrapper
loads `[vm]`, then `[table + 0x18]`, then performs an indirect `blx`.

The initial JNIEnv native table intentionally publishes no callable methods.
Its first five words are zero. The supplied MLA binary already proves the next
real requirement: its `JNIEnv::RegisterNatives` wrapper loads byte offset
`0x35c` (slot 215). That slot is reserved for the next JNI slice rather than
being filled speculatively here.

## Installation ownership

The caller owns all mappings and permission changes. Installation requires
nonzero aligned non-overlapping guest ranges, snapshots current bytes, writes
the VM/env tables and stub transactionally, and restores earlier writes if a
later write fails.

A typical mapped backend installs the stub while writable and then lets the
caller seal its page RX. The compatibility layer itself never changes mapping
permissions.

## GetEnv behavior

The private guest/host trap is SVC immediate `0xD7`.

The service models the current guest execution context as attached. It requires
the exact configured JavaVM pointer. Matching Dalvik, version validation occurs
before the output pointer is touched: the inclusive numeric JNI 1.1 through 1.6
range succeeds, while an out-of-range value returns JNI_EVERSION without
modifying `*env`.

For an accepted version the output slot must be writable, receives the logical
guest JNIEnv pointer, and the call returns JNI_OK.

## JNI_OnLoad transaction

`invoke_a32_jni_on_load` resolves `JNI_OnLoad` from one exact loaded object,
never from a dependency or global symbol scope. The function executes with
`r0=JavaVM*`, `r1=null`, caller-owned stack/stop PC, and bounded instruction
and service-call budgets.

The exact object index is optionally scoped through the existing ELF lifecycle
execution context for the duration of the guest call. This preserves automatic
DSO provenance for nested `__aeabi_atexit` registration and restores prior
context after return.

Dalvik/ART load semantics accept exactly JNI 1.2, 1.4, or 1.6 from JNI_OnLoad.
The transaction mirrors that rule.

## Validation

Focused host tests validate pointer-table bytes, transactional installation,
Dalvik GetEnv ordering/range, invalid guest pointers, exact-object symbol
isolation, OnLoad version checks, and resource options.

The dedicated pinned-NDK integration builds a dependency-free ARM32 shared
library whose JNI_OnLoad performs an indirect `JavaVM::GetEnv` call through
the guest table and returns JNI 1.6 only after receiving a non-null JNIEnv.
The host harness installs/seals the guest VM stub, proves lifecycle object
context is present during the SVC, and executes the fixture through the normal
A32 service dispatcher.

## Limits

This slice does not implement FindClass, RegisterNatives, Java class/reference
state, strings/arrays/exceptions, native-method dispatch, attach/detach,
JNI_OnUnload, Android framework services, graphics, or audio.
