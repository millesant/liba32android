# Compatibility spec delta — ARM32 JNI VM / JNI_OnLoad bootstrap

The first JNI compatibility surface provides one currently-attached ARM32 guest
JNI context. It does not model Java objects/classes.

## Guest ABI

The caller supplies already-mapped logical guest addresses for:

- one 4-byte JavaVM object;
- one 8-word JNIInvokeInterface prefix;
- one 4-byte JNIEnv object;
- one 5-word JNINativeInterface prefix; and
- one 8-byte ARM GetEnv SVC stub.

All five ranges must be nonzero, word aligned, pairwise non-overlapping, and fit
inside the 32-bit guest address space. Installation reads/snapshots and writes
only through GuestMemory. It never maps, unmaps, protects, or exposes a host
pointer. If a later install write fails, earlier writes are restored; rollback
failure is explicit.

The JavaVM object points to the invoke table. The invoke table follows Android
JNI order and publishes only slot 6 (byte offset 0x18), GetEnv. Other invoke
slots are zero. The JNIEnv object points to the native table; its first five
words (reserved0..3 and GetVersion) are zero in this slice.

The GetEnv target is one ARM stub: `svc #0xd7; bx lr`.

## JavaVM::GetEnv

The SVC handler requires the exact installed JavaVM logical guest pointer.

Matching Dalvik, version validation occurs before dereferencing the output
pointer. Any numeric value in the inclusive range
`JNI_VERSION_1_1 (0x00010001)` through
`JNI_VERSION_1_6 (0x00010006)` is accepted. For an in-range value, a non-null
writable output slot is required; the handler writes the exact configured
JNIEnv logical guest pointer and returns JNI_OK.

A version outside that range returns JNI_EVERSION and leaves the output slot
unchanged. An invalid JavaVM pointer or failed required guest write is a failed
host-service dispatch.

The current execution context is modeled as already attached. JNI_EDETACHED,
AttachCurrentThread, DetachCurrentThread, daemon attach, and DestroyJavaVM are
separate work.

## JNI_OnLoad

`invoke_a32_jni_on_load` targets one exact already-loaded graph object. It
builds that object's bounded symbol index and resolves `JNI_OnLoad` inside that
object only; dependencies/global scope never satisfy a missing target symbol.

The symbol must be a valid STT_FUNC guest address. Invocation uses r0=JavaVM*,
r1=null, the caller-provided aligned stack/return PC, and finite instruction and
service-call budgets through the accepted service-aware A32 executor.

When a lifecycle execution context is supplied, the exact target object index is
installed for the entire guest call and the previous context is restored on
return. Nested `__aeabi_atexit` registration can therefore retain automatic
DSO ownership provenance.

JNI_OnLoad succeeds only when its returned jint is exactly
JNI_VERSION_1_2, JNI_VERSION_1_4, or JNI_VERSION_1_6, matching Dalvik/ART
load-time behavior. Other returns are an explicit unsupported-version failure.

FindClass, RegisterNatives, native-method dispatch, Java references/objects,
strings/arrays/exceptions, thread attach/detach, JNI_OnUnload, and Android
framework classes are not provided by this slice.
