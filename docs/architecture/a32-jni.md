# ARM32 JNI compatibility

Status: raw static calls, bounded weak globals, ExceptionCheck, and GetStaticObjectField validated

## Goal

Provide a bounded guest-visible JNI ABI for real ARM32 Android native
libraries without introducing host-pointer identity or pretending that a full
Java VM already exists.

The design reuses logical guest memory, exact-object ELF symbol lookup, bounded
A32 service dispatch, and lifecycle execution context. Java-side state is added
only when native binaries prove they need it.

## Guest JavaVM and JNIEnv

`A32JniVmService` installs caller-addressed JavaVM/JNIEnv objects and their
function tables. All published pointers are logical 32-bit guest addresses; the
caller owns mappings and permission changes.

The JavaVM invocation table follows the Android ABI and now publishes
AttachCurrentThread at slot 4 / byte offset `0x10`, DetachCurrentThread at
slot 5 / byte offset `0x14`, and GetEnv at slot 6 / byte offset `0x18`.

The JNIEnv native table spans slots 0 through 228. Current evidence-backed
entries include:

- `FindClass` — slot 6 / byte offset `0x18`;
- `ThrowNew` — slot 14 / byte offset `0x38`;
- `ExceptionOccurred` — slot 15 / byte offset `0x3c`;
- `ExceptionClear` — slot 17 / byte offset `0x44`;
- `NewGlobalRef` — slot 21 / byte offset `0x54`;
- `DeleteGlobalRef` — slot 22 / byte offset `0x58`;
- `DeleteLocalRef` — slot 23 / byte offset `0x5c`;
- `NewObjectV` — slot 29 / byte offset `0x74`;
- `GetMethodID` — slot 33 / byte offset `0x84`;
- `CallVoidMethod` — slot 61 / byte offset `0xf4`;
- `CallVoidMethodV` — slot 62 / byte offset `0xf8`;
- `GetFieldID` — slot 94 / byte offset `0x178`;
- `GetIntField` — slot 100 / byte offset `0x190`;
- `GetLongField` — slot 101 / byte offset `0x194`;
- `SetLongField` — slot 110 / byte offset `0x1b8`;
- `GetStaticMethodID` — slot 113 / byte offset `0x1c4`;
- `GetStaticFieldID` — slot 144 / byte offset `0x240`;
- `GetStaticObjectField` — slot 145 / byte offset `0x244`;
- `GetStaticIntField` — slot 150 / byte offset `0x258`;
- `NewStringUTF` — slot 167 / byte offset `0x29c`;
- `GetStringUTFChars` — slot 169 / byte offset `0x2a4`;
- `ReleaseStringUTFChars` — slot 170 / byte offset `0x2a8`;
- `GetArrayLength` — slot 171 / byte offset `0x2ac`;
- `NewObjectArray` — slot 172 / byte offset `0x2b0`;
- `GetObjectArrayElement` — slot 173 / byte offset `0x2b4`;
- `SetObjectArrayElement` — slot 174 / byte offset `0x2b8`;
- `NewLongArray` — slot 180 / byte offset `0x2d0`;
- `GetByteArrayElements` — slot 184 / byte offset `0x2e0`;
- `GetLongArrayElements` — slot 188 / byte offset `0x2f0`;
- `ReleaseByteArrayElements` — slot 192 / byte offset `0x300`;
- `ReleaseLongArrayElements` — slot 196 / byte offset `0x310`;
- `SetLongArrayRegion` — slot 212 / byte offset `0x350`;
- `RegisterNatives` — slot 215 / byte offset `0x35c`;
- `NewWeakGlobalRef` — slot 226 / byte offset `0x388`;
- `DeleteWeakGlobalRef` — slot 227 / byte offset `0x38c`;
- `ExceptionCheck` — slot 228 / byte offset `0x390`.

Unsupported entries remain null.

The supplied ARMv7 `libmla.so` independently proves both JNIEnv offsets in
machine code, and also proves JavaVM::GetEnv at offset `0x18`.

## Installation ownership

The caller owns guest mappings and permission changes. Installation requires
nonzero aligned, non-overlapping ranges; snapshots current bytes; writes the
VM/env tables and private ARM service stubs transactionally; and restores
earlier writes if a later write fails.

A mapped backend can install stubs while writable and then seal the containing
pages RX. The compatibility layer itself does not map, unmap, or broaden page
permissions.

## Private service stubs

The current private guest/host service immediates are:

- `0xD7` — JavaVM::GetEnv;
- `0xD8` — JNIEnv::FindClass;
- `0xD9` — JNIEnv::RegisterNatives;
- `0xDA` — JNIEnv::GetMethodID;
- `0xDB` — JNIEnv::GetFieldID;
- `0xDC` — JNIEnv::GetStaticFieldID;
- `0xDD` — JavaVM::AttachCurrentThread;
- `0xDE` — JavaVM::DetachCurrentThread;
- `0xDF` — JNIEnv::NewGlobalRef;
- `0xE0` — JNIEnv::DeleteGlobalRef;
- `0xE1` — JNIEnv::DeleteLocalRef;
- `0xE2` — JNIEnv::GetArrayLength;
- `0xE3` — JNIEnv::GetStaticIntField;
- `0xE4` — JNIEnv::NewStringUTF;
- `0xE5` — JNIEnv::GetStringUTFChars;
- `0xE6` — JNIEnv::ReleaseStringUTFChars;
- `0xE7` — JNIEnv::NewLongArray;
- `0xE8` — JNIEnv::GetLongArrayElements;
- `0xE9` — JNIEnv::ReleaseLongArrayElements;
- `0xEA` — JNIEnv::SetLongArrayRegion;
- `0xEB` — JNIEnv::NewObjectArray;
- `0xEC` — JNIEnv::GetObjectArrayElement;
- `0xED` — JNIEnv::SetObjectArrayElement;
- `0xEE` — JNIEnv::GetLongField;
- `0xEF` — JNIEnv::SetLongField;
- `0xF0` — JNIEnv::ThrowNew;
- `0xF1` — JNIEnv::CallVoidMethodV;
- `0xF2` — JNIEnv::GetByteArrayElements;
- `0xF3` — JNIEnv::ReleaseByteArrayElements;
- `0xF4` — JNIEnv::CallVoidMethod.

Each guest stub is a minimal ARM `svc; bx lr` sequence. Unknown SVC immediates
remain unhandled.

## GetEnv behavior

GetEnv requires the exact configured JavaVM pointer. Matching Dalvik, version
validation happens before `*env` is touched: the inclusive numeric JNI 1.1
through 1.6 range succeeds, while an out-of-range value returns JNI_EVERSION
without modifying the output slot.

For an accepted version the output slot must be writable, receives the logical
guest JNIEnv pointer, and the call returns JNI_OK. While the modeled context is
detached, GetEnv instead returns JNI_EDETACHED without modifying the output
slot.

## JavaVM thread attachment

Supplied ARMv7 `libmla.so` wrappers directly identify AttachCurrentThread at
JavaVM slot 4 / offset `0x10` and DetachCurrentThread at slot 5 / offset
`0x14`.

The VM service now keeps a bounded table keyed by the same
`runtime::A32LogicalThreadId` used by pthread compatibility. Installation
creates one configured initial logical thread (default ID 1) in the attached
state so existing JNI_OnLoad/bootstrap behavior is preserved. The embedding
selects the active logical identity through `set_current_thread_id` or
`set_current_thread_context`; no host TID or host pthread identity is used.

GetEnv, AttachCurrentThread, DetachCurrentThread, and every JNIEnv-native
service apply to that selected logical thread. GetEnv returns JNI_EDETACHED
without touching `*env` when the selected identity has no attached state.
AttachCurrentThread creates or restores bounded state and writes the configured
logical JNIEnv pointer. DetachCurrentThread releases only that logical thread's
guest-created local references, pending exception, and owned scratch leases.
Other logical threads and VM-wide global/weak-global references are unchanged.

The default logical-thread ceiling is 64 with a hard ceiling of 1024.
`A32JniVmService` also implements the existing `A32PthreadThreadExitHook`;
pthread exit invokes the same cleanup path after pthread cleanup handlers/TLS
destructors and before the logical thread is published Exited.

AttachCurrentThreadAsDaemon, JavaVMAttachArgs contents, host-thread passthrough,
and Java Thread objects remain outside this slice.

See
[ARM32 JNI JavaVM thread entrypoint evidence](../research/evidence/arm32-jni-thread-entrypoints-2026-09-29.md).

## Strong/local reference bookkeeping

Supplied ARMv7 `libmla.so` wrappers directly identify NewGlobalRef at JNIEnv
slot 21 / offset `0x54`, DeleteGlobalRef at slot 22 / `0x58`, and
DeleteLocalRef at slot 23 / `0x5c`.

The bounded registry keeps opaque logical object identities and aggregate
local/global/weak counts, while `A32JniVmService` records ownership of every
guest-created local reference by logical thread. Successful FindClass and
object/string/array-returning JNI paths charge their local reference to the
selected attached thread. DeleteLocalRef can release only that thread's owned
guest local, so one logical thread cannot consume another thread's local
reference. References explicitly seeded by the embedding outside JNI service
calls retain legacy initial-thread behavior for deterministic host fixtures.

Global and weak-global counts remain VM-wide. NewGlobalRef/NewWeakGlobalRef
accept a non-null source only when it is a selected-thread local or a VM-wide
strong global. Global/weak deletion is visible across attached logical threads.
Detach and pthread exit release the departing thread's guest locals without
damaging VM-wide globals or surviving threads.

The representation remains APK-agnostic and never exposes host pointers.
Local frames/capacity APIs, NewLocalRef, IsSameObject, garbage collection,
automatic weak clearing, and universal Java heap reachability remain separate
work.

See
[ARM32 JNI reference entrypoint evidence](../research/evidence/arm32-jni-reference-entrypoints-2026-09-29.md).

## Class registry and FindClass

`A32JniClassRegistry` is caller-owned and borrowed by the VM service. It keeps
explicit limits for class count, registered native count, per-registration
method count, and guest string lengths.

Classes are associated with caller-selected nonzero logical `jclass` handles.
Class names and handles must be unique.

FindClass:

- requires the exact configured JNIEnv pointer;
- copies a bounded NUL-terminated guest class name through `GuestMemory`;
- returns the exact registered guest handle on a hit;
- returns null on a semantic miss;
- treats unreadable/unterminated guest strings as service failures.

No host pointer is exposed as a `jclass`.

## RegisterNatives

RegisterNatives requires the exact configured JNIEnv pointer and a registered
class handle. ARM32 `JNINativeMethod` records are decoded as three 32-bit
little-endian words:

```text
name pointer
signature pointer
guest function pointer
```

The method count and all guest strings are bounded. The complete registration
call is decoded and validated before registry mutation. Duplicate keys inside a
single call are rejected, and a failed transaction does not partially update
the registry.

Accepted metadata owns copies of class/name/signature strings while preserving
the native function as a logical 32-bit guest function pointer.

## Observed member-ID lookup

The next bounded identity surface is driven by supplied ARMv7 `libmla.so`
machine code rather than the complete JNI table. Direct wrappers load:

- GetMethodID from slot 33 / byte offset `0x84`;
- GetFieldID from slot 94 / byte offset `0x178`;
- GetStaticFieldID from slot 144 / byte offset `0x240`.

The implemented slice models those IDs as caller-seeded logical 32-bit handles
in the existing bounded registry. Lookup is exact over class, member kind, name,
and signature. Exact-head validation at
`f073092cf631a98e274bce9cdb54d0f73ad7099d` passed all 11 required checks.
It does not imply Java method invocation, field access, inheritance, or
object/reference lifetime.

See
[ARM32 JNI member-ID entrypoint evidence](../research/evidence/arm32-jni-member-id-entrypoints-2026-09-28.md).

## Seeded array length

Supplied ARMv7 `libmla.so` directly loads GetArrayLength from JNIEnv slot 171 /
offset `0x2ac`.

The bounded array seam is metadata-only. The caller seeds one logical array
handle plus a finite signed-32-bit-compatible length. Seeding establishes one
local reference through the generic reference ledger. GetArrayLength returns
that exact length for the known logical array and fails for null, unknown, or
non-array handles. Exact-head validation at
`db558233a50eb79c21e65792dea4a4b74bd72d89` passed all 11 required checks.

No array allocation, element storage/access, pin/copy buffer, or Java array type
semantics are implied.

See
[ARM32 JNI GetArrayLength evidence](../research/evidence/arm32-jni-array-length-entrypoint-2026-09-29.md).

## Static int field values

Supplied ARMv7 `libmla.so` directly loads GetStaticIntField from JNIEnv slot
150 / offset `0x258`.

The bounded member registry keeps the already-accepted opaque StaticField ID and
may associate one caller-seeded signed 32-bit value with it. GetStaticIntField
validates the exact JNIEnv, attached state, class/member association, and static
field kind before returning the exact jint bits. Missing or mismatched logical
Java state fails rather than being synthesized.

The implementation remains APK-agnostic: class/member/value identities are
supplied by the embedding and no package names, reflection, host objects, or
framework behavior are encoded in `src/compat`.

See
[ARM32 JNI GetStaticIntField evidence](../research/evidence/arm32-jni-static-int-field-entrypoint-2026-09-29.md).

## Modified-UTF-8 strings

Supplied ARMv7 `libmla.so` directly identifies NewStringUTF at JNIEnv slot
167 / offset `0x29c`, GetStringUTFChars at slot 169 / `0x2a4`, and
ReleaseStringUTFChars at slot 170 / `0x2a8`.

NewStringUTF copies one bounded guest byte payload into owned registry storage
and allocates a synthetic logical jstring handle from a caller-configurable
range. The handle participates in the generic local/global reference ledger;
host string pointers are never guest-visible.

GetStringUTFChars copies the stored bytes plus NUL into one caller-owned mapped
guest scratch region and reports JNI_TRUE through non-null `isCopy`. This
bounded slice allows one outstanding UTF-char lease at a time.
ReleaseStringUTFChars requires the exact leased string/scratch pair and closes
that lease.

The slice preserves byte payloads as supplied. UTF-16 conversion, full
modified-UTF-8 validation, GetStringUTFLength, region APIs, and multiple
simultaneous leases remain separate.

See
[ARM32 JNI modified-UTF-8 evidence](../research/evidence/arm32-jni-modified-utf8-entrypoints-2026-09-29.md).

Exact-head validation at
`d06d2ec0393c0cb12fb414d07d618b5bf1c7f07d` passed all 11 required checks.

## jlong arrays

Supplied ARMv7 `libmla.so` identifies one coherent primitive-array family:
NewLongArray at slot 180, GetLongArrayElements at slot 188,
ReleaseLongArrayElements at slot 196, and SetLongArrayRegion at slot 212.

NewLongArray allocates a synthetic logical handle, zero-initialized owned
`int64_t` storage, generic array-length metadata, and one local reference.
GetLongArrayElements copies the full array into one caller-owned 8-byte-aligned
guest scratch region and records an exact lease. ReleaseLongArrayElements
implements copy-back/release mode 0, JNI_COMMIT, and JNI_ABORT. No host element
pointer becomes guest-visible.

SetLongArrayRegion decodes start/length as signed jsize values and reads the
fifth ARM32 argument from guest `[sp]` as the source jlong pointer. Values are
decoded little-endian and written only after the range is validated.

See
[ARM32 JNI jlong-array evidence](../research/evidence/arm32-jni-long-array-entrypoints-2026-09-29.md).

Exact-head validation at
`94dd3ed5155654956decce93dd6cbe73c25d4cf0` passed all 11 required checks.

## Object arrays

Supplied ARMv7 `libmla.so` directly identifies NewObjectArray at JNIEnv slot
172 / offset `0x2b0`, GetObjectArrayElement at slot 173 / `0x2b4`, and
SetObjectArrayElement at slot 174 / `0x2b8`.

The bounded registry allocates a synthetic object-array handle, retains one
local reference for the array, stores a registered element-class handle, and
owns a finite vector of opaque logical jobject identities. GetArrayLength reuses
the generic array-length metadata.

NewObjectArray accepts null or a currently live initial logical reference.
GetObjectArrayElement returns null for a null entry; a non-null stored identity
receives one local JNI reference before the same opaque handle is returned.
SetObjectArrayElement accepts null or a currently live logical identity and does
not alter the caller's local/global reference count merely because the identity
is stored.

The runtime still has no Java inheritance/assignability graph. This slice
therefore does not synthesize ArrayStoreException or claim full Java object
semantics.

See
[ARM32 JNI object-array evidence](../research/evidence/arm32-jni-object-array-entrypoints-2026-09-29.md).

Exact-head validation at
`42d542ab8a14ae11ff534c6f7734f972280741c1` passed all 11 required checks.

## Instance long fields

Supplied ARMv7 `libmla.so` identifies GetLongField at JNIEnv slot 101 /
offset `0x194` and SetLongField at slot 110 / `0x1b8`.

The bounded registry stores signed 64-bit values by exact logical jobject
identity plus an existing InstanceField jfieldID. Service calls require the
object identity to be currently live through the generic local/global reference
ledger. Missing value state fails rather than inventing Java object state.

GetLongField returns the exact 64-bit value through ARM32 r0/r1. SetLongField
follows AAPCS32 alignment and decodes its jlong input from guest `[sp]` and
`[sp+4]` after JNIEnv/object/field occupy r0-r2.

The runtime still does not map arbitrary jobject identities to Java classes or
field offsets, so object/class assignability, inheritance, volatile semantics,
and reflection remain outside this slice.

See
[ARM32 JNI instance-long-field evidence](../research/evidence/arm32-jni-instance-long-field-entrypoints-2026-09-29.md).

Exact-head validation at
`114d9d104ee82d1e30aa78fa785c8b389e8630db` passed all 11 required checks.

## Pending ThrowNew state

Supplied ARMv7 `libmla.so` directly identifies ThrowNew at JNIEnv slot 14 /
offset `0x38`.

Each attached logical guest thread can hold at most one pending logical
exception: registered class handle, owned class name, owned bounded message
bytes, and one bounded logical exception identity. The selected thread's
pending state is presented through the existing registry seam; switching
logical threads stashes/restores that state without exposing it to another
thread. ThrowNew requires a currently usable logical jclass reference. The
first throw on a thread records state and returns JNI_OK; a second throw on the
same thread returns JNI_ERR and preserves that thread's original exception.

ExceptionOccurred, ExceptionCheck, and ExceptionClear operate on the selected
logical thread's pending state. Detach and pthread exit discard only the
departing thread's unobserved pending identity; local/global references already
returned by ExceptionOccurred retain their ordinary reference lifetime.

This remains deliberately smaller than a Java Throwable runtime: no host
Throwable pointer, stack trace, Java-frame unwinding, or universal ART
pending-exception gating is synthesized.

See
[ARM32 JNI ThrowNew evidence](../research/evidence/arm32-jni-throw-new-entrypoint-2026-09-29.md).

Exact-head validation at
`361b9ffb044d5ed4a6cdfa080f3e93bec7893c9d` passed all 11 required checks.

## CallVoidMethodV bridge

The supplied VLC ARMv7 `libmla.so` C++ `_JNIEnv::CallVoidMethod(...)`
wrapper directly loads native-table byte offset `0xf8`, slot 62, then calls
that function as CallVoidMethodV with JNIEnv, receiver, method ID, and the
constructed ARM32 `va_list` in r0-r3.

The compatibility service publishes that exact V slot through private SVC
`0xF1`. It requires a live logical receiver and an existing InstanceMethod ID.
The method descriptor drives a bounded ARM32 `va_list` decoder: promoted
32-bit integral values consume one word, jlong/jdouble values use AAPCS32
8-byte alignment, jfloat is consumed as its C default-promoted double and
narrowed, and object/array descriptors produce logical reference handles.

Decoded arguments are normalized into typed logical values and passed to a
caller-owned `A32JniMethodCallBridge`. The bridge is the Java-behavior
boundary; liba32android does not fabricate a Java VM implementation. Non-null
reference arguments must already be live in the bounded reference ledger.

Raw variadic CallVoidMethod slot 61, CallVoidMethodA, return-valued/static or
nonvirtual method families, NewObject, inheritance/virtual dispatch, and Java
framework behavior remain separate.

See
[ARM32 JNI CallVoidMethodV evidence](../research/evidence/arm32-jni-call-void-method-v-entrypoint-2026-09-29.md).

Exact-head validation at
`8a528b9402a874e8d1520687dc5920248234af7b` passed all 11 required checks.

## Raw CallVoidMethod bridge

The supplied VLC ARMv7 `libvlcjni.so` `VLCJniObject_attachEvents` helper
directly loads native-table byte offset `0xf4`, slot 61, then invokes raw
CallVoidMethod with JNIEnv, receiver, and method ID in r0-r2. The first
variadic 32-bit word is forwarded in r3 and later arguments are staged on the
guest stack. The same call site promotes a float source to double before
placing it at an 8-byte-aligned stack address.

The raw decoder shares the accepted descriptor parser and
`A32JniMethodCallBridge` value surface with CallVoidMethodV. Promoted
32-bit integral/reference values consume r3 first and then stack words.
jlong/jdouble/default-promoted jfloat values do not split across odd r3 and the
stack; they begin at the next 8-byte-aligned guest stack location. Non-null
logical reference arguments must be live before the embedding bridge is called.

CallVoidMethodA, return-valued/static/nonvirtual method families, NewObject,
inheritance/virtual dispatch, and Java framework behavior remain separate.

See
[ARM32 JNI raw CallVoidMethod evidence](../research/evidence/arm32-jni-call-void-method-entrypoint-2026-09-30.md).

Exact-head validation at
`1383d7cd44b3b0a669e9e2a3e6fd7915d747efcc` passed all 11 required checks.

## Byte-array element leases

Supplied ARMv7 `libfmod.so` directly identifies GetByteArrayElements at
JNIEnv slot 184 / byte offset `0x2e0` and ReleaseByteArrayElements at slot
192 / `0x300`.

The bounded registry accepts caller-seeded logical jbyteArray handles with
owned byte storage, generic GetArrayLength metadata, and ordinary JNI
local/global liveness accounting. The VM layout provides one caller-owned guest
scratch byte region. GetByteArrayElements requires a live known array and no
outstanding byte lease, copies the bytes to scratch, reports JNI_TRUE through a
non-null isCopy output, and returns the logical scratch address.

ReleaseByteArrayElements requires the exact leased array and exact scratch
pointer. Mode 0 copies back and releases, JNI_COMMIT copies back and retains the
lease, and JNI_ABORT releases without copying guest changes. NewByteArray,
byte-region APIs, other primitive-array families, pinning, and simultaneous
byte leases remain separate.

See
[ARM32 JNI byte-array evidence](../research/evidence/arm32-jni-byte-array-elements-entrypoint-2026-09-30.md).

Exact-head validation at
`4e8326b03a8f9180700e9715126b05081ddee7b9` passed all 11 required checks.

## Reverse native dispatch

`invoke_a32_registered_native_noargs` is the first deliberately narrow
host-to-guest native dispatch transaction.

It resolves one exact class/name/signature binding, currently accepts only
zero-Java-argument signatures, supplies `JNIEnv*` in `r0` and the caller's
receiver/class handle in `r1`, and executes through the existing bounded
service-aware A32 executor.

The caller provides stack top, stop PC, instruction budget, and service-call
budget. The raw `r0` return bits are exposed to the caller.

This is not yet general JNI argument marshalling.

## JNI_OnLoad transaction

`invoke_a32_jni_on_load` resolves `JNI_OnLoad` from one exact loaded object,
never from a dependency or global symbol scope. The function executes with
`r0=JavaVM*`, `r1=null`, caller-owned stack/stop PC, and bounded instruction and
service-call budgets.

The exact object index is optionally scoped through the existing ELF lifecycle
execution context for the duration of the guest call. This preserves automatic
DSO provenance for nested `__aeabi_atexit` registration and restores prior
context after return.

Dalvik/ART load semantics accept exactly JNI 1.2, 1.4, or 1.6 from JNI_OnLoad.
The transaction mirrors that rule.

## JNI_OnUnload transaction

`invoke_a32_jni_on_unload` reuses the exact-object symbol and bounded
service-aware execution boundary for the optional
`void JNI_OnUnload(JavaVM*, void*)` hook. It resolves only the requested
loaded object, executes with r0=JavaVM* and r1=null, scopes optional lifecycle
provenance to that object, and requires the caller stop PC to be reached within
the configured instruction/service budgets.

Unlike JNI_OnLoad, there is no returned-version validation. The supplied VLC
ARMv7 `libmla.so` and `libvlcjni.so` exports prove the hook is relevant;
`libvlcjni.so` acquires JNIEnv through GetEnv and deletes stored global
references during unload.

This is explicit invocation, not generic `dlclose` policy. JNI_OnUnload is a
VM/class-loader lifecycle hook, so automatic triggering requires a separate
Java-library ownership model rather than being inferred from ELF handle count.

## Validation state

Host regressions cover:

- VM/env table and service-stub bytes;
- invalid/overlapping layout and transactional rollback;
- GetEnv version/output ordering and pointer validation;
- FindClass hit/miss/wrong-env behavior;
- bounded RegisterNatives parsing and transactional rejection;
- exact registered-native lookup;
- one zero-argument reverse-dispatch execution;
- exact-object JNI_OnLoad isolation and return-version validation;
- exact-object JNI_OnUnload isolation and bounded void execution.

The pinned-NDK ARM32 integration fixture now performs the complete bounded
registration path: JNI_OnLoad calls GetEnv, FindClass, and RegisterNatives
through the installed guest tables. The host verifies the exact
`org/videolan/Fixture / nativePing / ()I` binding and then invokes that
registered guest native through reverse dispatch, which returns 42.

Exact-head validation at
`dbbcb06d5a9e0225b9e0a9c3515b646c0fff6503` passed all 11 required checks,
including the ARM32 JNI registration integration.

## Limits

The current compatibility surface still does not provide a general Java object
runtime, inheritance/virtual dispatch, A-form or return-valued method-call
families, general native argument marshalling, automatic VM/class-loader JNI library
unload ownership, framework classes, graphics, or audio. Existing references, members, strings/arrays, fields,
thread attachment, pending ThrowNew state, CallVoidMethodV, and byte-array
element leases are deliberately bounded seams rather than full Java semantics.

## Continuous JNI roadmap

JNI is one continuous compatibility track. Each bounded slice keeps the guest
ARM32 ABI stable, reuses the service-aware executor, and adds only Java-side
state that real native libraries prove they need.

Planned order after the registration slice:

1. **Class/member identity substrate**
   - GetObjectClass and IsInstanceOf;
   - deterministic guest `jclass` / `jmethodID` / `jfieldID` handles;
   - GetMethodID/GetStaticMethodID/GetFieldID/GetStaticFieldID.

2. **Reference model**
   - local/global/weak references;
   - local frames/capacity;
   - IsSameObject and reference-type queries;
   - explicit lifetime/count ceilings.

3. **Strings**
   - NewString/NewStringUTF;
   - UTF-16 and modified-UTF-8 length/access/release paths;
   - region APIs with bounded copies.

4. **Arrays**
   - primitive/object array creation and length;
   - element/region access and release behavior;
   - object-array element references.

5. **Exceptions**
   - Throw/ThrowNew;
   - ExceptionOccurred/Check/Clear/Describe;
   - bounded pending-exception state per guest execution context.

6. **Object construction and method calls**
   - NewObject[A/V];
   - Call<type>Method[A/V], nonvirtual, and static families;
   - normalize ARM32 varargs to one internal `jvalue[]` path.

7. **Fields**
   - Get/Set<type>Field;
   - static-field families;
   - deterministic backing storage for compatibility-model classes.

8. **Thread/VM invocation surface**
   - AttachCurrentThread / DetachCurrentThread;
   - GetEnv detached behavior;
   - daemon attach and DestroyJavaVM only if real evidence requires them.

9. **Direct buffers, critical access, monitors**
   - NewDirectByteBuffer / address / capacity;
   - primitive/string critical APIs;
   - MonitorEnter / MonitorExit.

10. **Compatibility completion**
    - inspect VLC/MLA/FMOD and later APK evidence after every slice;
    - fill remaining JNI table slots only when real binaries require them;
    - keep Android framework-class behavior distinct from generic JNI ABI
      plumbing.

The goal is broad native-facing JNI compatibility, not reimplementation of ART.

## Instance int fields

Direct supplied VLC ARMv7 `libvlcjni.so` evidence selects
`GetIntField` at JNIEnv slot 100 / `0x190`. Caller-seeded signed 32-bit
values are keyed by a live logical jobject and an existing InstanceField ID.
The service validates exact JNIEnv/attachment, reference liveness, member kind,
and value presence before returning the exact jint bits in r0.

This does not model Java object layout, class assignability, inheritance,
volatile semantics, or `SetIntField`.

## Exception observation and clear

Direct supplied VLC ARMv7 `libvlcjni.so` evidence selects
`ExceptionOccurred` at slot 15 / `0x3c` and `ExceptionClear` at slot 17 /
`0x44`.

A successful ThrowNew now reserves one bounded logical exception identity in a
dedicated handle namespace without creating an implicit local reference.
`ExceptionOccurred` returns null when no exception is pending; otherwise it
returns that exact logical identity and creates one local reference while
leaving the exception pending. `ExceptionClear` clears pending state but
preserves any local/global reference already returned to the guest. An
unobserved zero-reference pending identity is reclaimed when cleared.

No host Throwable pointer, Java stack trace, unwinding, ExceptionDescribe, or
automatic global JNI exception gating is introduced. ExceptionCheck is now
implemented separately as a read-only observation of this pending state.

## NewObjectV construction bridge

The supplied ARMv7 `libmla.so` C++ `_JNIEnv::NewObject(...)` wrapper
constructs a `va_list` and loads JNIEnv byte offset `0x74`, slot 29, before
the indirect call. That is the ABI's `NewObjectV` entry; raw NewObject slot 28
remains outside this slice.

The existing bounded JNI descriptor/`va_list` decoder is reused for constructor
arguments. The service requires a live logical jclass, an InstanceMethod ID
owned by that exact class, constructor name `<init>`, a void-return descriptor,
and the existing embedding-owned method-call bridge. The bridge may return a
fresh nonzero logical jobject handle; the registry accepts it only if it is not
already an identity, then creates exactly one local reference.

No host object pointer, Java heap layout, inheritance/assignability engine,
constructor bytecode execution, raw NewObject, or NewObjectA is introduced.

## Static method IDs

The supplied VLC ARMv7 `libvlcjni.so` JNI_OnLoad directly loads JNIEnv byte
offset `0x1c4`, slot 113, before multiple indirect calls with JNIEnv, jclass,
method name, and signature in r0-r3. This establishes `GetStaticMethodID`
without relying on table adjacency.

The bounded member registry adds a distinct StaticMethod kind so static and
instance methods with the same class/name/signature remain different logical
identities. `GetStaticMethodID` reuses the existing bounded guest-string and
exact class/name/signature lookup path; semantic misses return null and guest
memory faults fail the service.

Static method invocation remains separate even though supplied VLC callsites
also directly expose CallStaticObjectMethod and CallStaticVoidMethod.

## Static void method calls

Supplied VLC ARMv7 `libvlcjni.so` directly selects raw
`CallStaticVoidMethod` at JNIEnv slot 141 / byte offset `0x234`. The service
requires a live exact logical jclass plus an existing StaticMethod ID for that
class, decodes raw ARM32 variadic arguments with the accepted descriptor/AAPCS32
decoder, validates non-null logical reference arguments, and invokes the
caller-owned synchronous method-call bridge.

This slice does not publish CallStaticVoidMethodV/A, return-valued static call
families, Java dispatch/class initialization, or framework behavior.

## Static object method calls

The supplied VLC ARMv7 `libvlcjni.so` directly selects raw
`CallStaticObjectMethod` at JNIEnv slot 114 / byte offset `0x1c8`.
The service reuses exact live jclass/StaticMethod validation and the bounded raw
r3-plus-stack AAPCS32 argument decoder.

The caller-owned synchronous bridge may return null or one pre-existing logical
JNI reference identity. A non-null identity must already be known to the
registry and receives one local JNI reference before it is returned in r0.
Unknown identities fail rather than creating implicit Java heap state.

CallStaticObjectMethodV/A, other return-valued static families, Java class
initialization/dispatch, and framework object creation remain separate.

## Weak global references

Balanced supplied VLC ARMv7 evidence identifies `NewWeakGlobalRef` at JNIEnv
slot 226 / `0x388` in `VLCJniObject_newFromLibVlc` and
`DeleteWeakGlobalRef` at slot 227 / `0x38c` in
`VLCJniObject_release`.

The bounded registry uses the same logical object handle for the weak alias and
tracks weak ownership separately from local/global strong counts. A weak
reference can be created only from a currently strong-live logical identity and
does not contribute to strong liveness. DeleteWeakGlobalRef removes only weak
ownership.

This slice does not model garbage collection, automatic weak clearing,
resurrection, NewLocalRef-from-jweak, IsSameObject, or Java heap reachability.

## ExceptionCheck observation

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly selects JNIEnv slot 228 /
`0x390`. The service reads only the bounded pending-exception state and
returns JNI_FALSE or JNI_TRUE. It creates no local/global reference, preserves
the existing pending exception unchanged, and composes with the accepted
ExceptionOccurred/ExceptionClear model.

The pinned ARM32 JNI fixture executes the real slot-228 call in its no-pending
path. Exact-head validation passed with the updated expected service-call count.

## Static object fields

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly selects
`GetStaticObjectField` at JNIEnv slot 145 / `0x244` after resolving the
field through GetStaticFieldID. The returned object is immediately consumed by
GetStringUTFChars and later DeleteLocalRef, establishing object-return/local-ref
behavior for the observed path.

The bounded registry stores an explicitly seeded null or pre-existing logical
object identity against an existing StaticField ID. The stored field identity
is independent of caller local/global JNI counts. A non-null read creates one
local reference before returning the same opaque logical handle, so deleting a
prior returned local reference does not erase the static field value.

No host pointer, Java class initialization, SetStaticObjectField, descriptor
type engine, inheritance/assignability model, garbage collector, or framework
object implementation is introduced.
