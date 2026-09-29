# ARM32 JNI compatibility

Status: member IDs validated; evidence-backed JavaVM AttachCurrentThread/DetachCurrentThread slice in progress

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

The JNIEnv native table now spans slots 0 through 215 so the first two
evidence-backed entries can be published at their real positions:

- `FindClass` — slot 6 / byte offset `0x18`;
- `RegisterNatives` — slot 215 / byte offset `0x35c`.

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
- `0xDE` — JavaVM::DetachCurrentThread.

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

The service continues to model one bounded guest JNI context rather than a
general host-thread registry. Installation starts attached. Detach transitions
the context to detached; GetEnv then returns JNI_EDETACHED for supported
versions without touching `*env`. AttachCurrentThread writes the configured
logical JNIEnv pointer and restores attached state. JNIEnv-native services are
rejected while detached.

AttachCurrentThreadAsDaemon, JavaVMAttachArgs semantics, multiple host threads,
and thread-local Java reference state remain outside this slice.

See
[ARM32 JNI JavaVM thread entrypoint evidence](../research/evidence/arm32-jni-thread-entrypoints-2026-09-29.md).

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

## Validation state

Host regressions cover:

- VM/env table and service-stub bytes;
- invalid/overlapping layout and transactional rollback;
- GetEnv version/output ordering and pointer validation;
- FindClass hit/miss/wrong-env behavior;
- bounded RegisterNatives parsing and transactional rejection;
- exact registered-native lookup;
- one zero-argument reverse-dispatch execution;
- exact-object JNI_OnLoad isolation and return-version validation.

The pinned-NDK ARM32 integration fixture now performs the complete bounded
registration path: JNI_OnLoad calls GetEnv, FindClass, and RegisterNatives
through the installed guest tables. The host verifies the exact
`org/videolan/Fixture / nativePing / ()I` binding and then invokes that
registered guest native through reverse dispatch, which returns 42.

Exact-head validation at
`dbbcb06d5a9e0225b9e0a9c3515b646c0fff6503` passed all 11 required checks,
including the ARM32 JNI registration integration.

## Limits

The current slice does not provide general Java object/reference lifetime,
member IDs, strings/arrays, pending exceptions, Java method/field calls,
general native argument marshalling, thread attach/detach, JNI_OnUnload,
framework classes, graphics, or audio.

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
