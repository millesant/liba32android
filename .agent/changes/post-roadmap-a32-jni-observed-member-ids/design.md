# Design — ARM32 JNI observed member IDs

## Evidence-backed table entries

The supplied ARMv7 libmla.so loads these JNIEnv native-interface offsets before
indirect calls:

- GetMethodID: byte offset 0x84, slot 33;
- GetFieldID: byte offset 0x178, slot 94;
- GetStaticFieldID: byte offset 0x240, slot 144.

The existing 216-word JNIEnv table already spans all three positions. Each entry
gets a distinct private ARM svc/bx-lr stub. Unsupported entries remain null.

## Registry model

A32JniClassRegistry remains caller-owned. It gains a bounded collection of
member identities. Each record owns class/name/signature strings and stores only
caller-selected nonzero logical 32-bit handles.

Member kind is explicit: instance method, instance field, or static field. A
member handle is unique across the registry, and the exact
class/kind/name/signature key is unique.

## Lookup services

For each observed member-ID JNI call:

- r0 must equal the configured JNIEnv pointer;
- r1 is the registered logical jclass handle;
- r2 points to a bounded NUL-terminated member name;
- r3 points to a bounded NUL-terminated signature.

An exact match returns the configured logical member handle. Unknown
class/member identity is a semantic miss and returns null. Unreadable or
unterminated guest strings are service failures.

## Deliberate boundary

This slice creates identity only. It does not create objects, invoke Java
methods, read/write fields, model inheritance, or assign reference lifetime.
GetStaticMethodID/GetObjectClass/IsInstanceOf remain null until evidence or a
later bounded contract requires them.
