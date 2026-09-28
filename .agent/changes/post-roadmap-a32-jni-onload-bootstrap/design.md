# Design — ARM32 JNI VM / JNI_OnLoad bootstrap

Keep JNI bootstrap above the generic ELF loader and below Java object semantics.

## Guest-visible ABI

The caller supplies already-mapped logical guest addresses for:

- one 32-bit `JavaVM` object;
- one 8-word `JNIInvokeInterface` prefix;
- one 32-bit `JNIEnv` object;
- one 5-word `JNINativeInterface` prefix (four reserved words plus the
  currently-null GetVersion slot); and
- one 8-byte ARM GetEnv stub.

The JavaVM object contains the invoke-table pointer. The JNIInvokeInterface uses
the Android/JNI layout: reserved0..2, DestroyJavaVM, AttachCurrentThread,
DetachCurrentThread, GetEnv, AttachCurrentThreadAsDaemon. Only GetEnv is
published in this slice. The JNIEnv object contains the native-interface pointer;
native-interface function slots are intentionally unsupported/null.

The GetEnv entry points to `svc #0xd7; bx lr`. The caller may install while
the stub page is writable and then seal it RX; the compatibility layer itself
does not own mapping or permissions.

All guest ranges must be nonzero, word aligned, non-overlapping and fit in the
32-bit address space.

## GetEnv

The service accepts only the exact configured JavaVM address in r0 and a
non-null writable 32-bit `void**` slot in r1. r2 is the requested JNI version.

Matching Dalvik, GetEnv accepts the inclusive numeric range from
JNI_VERSION_1_1 through JNI_VERSION_1_6. For an in-range value the service
requires a writable output slot, writes the configured JNIEnv logical guest
pointer, and returns JNI_OK (0). For a version outside that range it returns
JNI_EVERSION (-3) before touching the output slot. An invalid VM pointer or
failed required guest write is a host-service failure, not a fabricated JNI
result.

This first VM represents the currently executing guest context as attached.
Attach/Detach/Destroy and daemon attach remain null/unimplemented.

## JNI_OnLoad transaction

The transaction takes one exact loaded graph object index. It builds that
object's bounded dynamic-symbol index and resolves `JNI_OnLoad` within that
object only, never through dependency/global scope.

The resolved guest function is invoked with r0=JavaVM*, r1=0, caller stack,
caller stop PC, and finite total instruction/service budgets through the
existing service-aware A32 executor. ARM/Thumb state follows symbol bit 0.

When the caller supplies the existing lifecycle execution context, the
transaction scopes its object index to the exact JNI_OnLoad object for the
entire guest call and restores the prior value afterward. Nested
`__aeabi_atexit` registration can therefore retain the same automatic DSO
ownership provenance as constructor execution.

Success requires the returned jint to be exactly JNI_VERSION_1_2,
JNI_VERSION_1_4, or JNI_VERSION_1_6, matching Dalvik's load-time contract.
There is no Java class-loader/object model in this slice.

## Evidence-driven next seam

The supplied ARMv7 libvlc.so, libmla.so, and libfmod.so export JNI_OnLoad.
libmla.so additionally contains C++ JNI wrappers for JavaVM::GetEnv and
JNIEnv::RegisterNatives. Therefore RegisterNatives/class lookup is the next
evidence-backed JNI seam after GetEnv/JNI_OnLoad, not part of this slice.
