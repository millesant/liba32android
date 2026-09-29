# Compatibility spec delta — ARM32 JNI observed member IDs

The JNIEnv native table publishes the evidence-backed member-ID entries:

- GetMethodID at slot 33 / byte offset 0x84;
- GetFieldID at slot 94 / byte offset 0x178;
- GetStaticFieldID at slot 144 / byte offset 0x240.

Each points at a distinct private guest ARM service stub. Unsupported JNIEnv
entries remain zero.

A caller-owned bounded registry may seed member identities under explicit member
count, name-length, and signature-length ceilings. Each member record has an
exact registered class, explicit kind, unique nonzero logical 32-bit handle,
owned name/signature strings, and no host pointer identity.

Member lookup requires the exact configured JNIEnv pointer and registered class.
It copies the guest name/signature through GuestMemory under registry ceilings.
Exact class/kind/name/signature matches return the logical handle. Unknown
class/member matches return null. Guest memory faults and unterminated strings
fail the service.

This slice does not implement GetStaticMethodID, GetObjectClass, IsInstanceOf,
method invocation, field access, Java object/reference lifetime, inheritance,
strings/arrays/exceptions, or framework classes.
