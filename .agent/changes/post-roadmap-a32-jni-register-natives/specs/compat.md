# Compatibility spec delta — ARM32 JNI registration bootstrap

The JNIEnv native table contains 216 32-bit words (slots 0 through 215).
FindClass is published at slot 6 / byte offset 0x18 and RegisterNatives at slot
215 / byte offset 0x35c. All unsupported entries remain zero.

A caller-owned registry supplies exact slash-separated class names and nonzero
logical guest jclass handles. Registry capacities and guest string lengths are
finite caller-selected values subject to implementation hard ceilings.

FindClass accepts the exact configured JNIEnv pointer and a bounded guest
NUL-terminated class name. A registered name returns its exact jclass handle;
an absent name returns null. Unreadable or unterminated guest strings fail the
host service instead of manufacturing Java state.

RegisterNatives accepts the exact configured JNIEnv pointer, a registered
jclass, an ARM32 JNINativeMethod array, and a nonnegative count. Each record is
three little-endian 32-bit words: name pointer, signature pointer, function
pointer. The whole call is decoded and validated before registry mutation.
Malformed memory fails the host service; semantic rejection returns JNI_ERR.
Successful registration returns JNI_OK.

Registered bindings own their class/name/signature metadata and store only the
logical guest function pointer. Exact class/name/signature lookup is
deterministic.

The first reverse-dispatch API is deliberately narrow: it invokes only an exact
registered signature with zero Java arguments, supplying JNIEnv* in r0 and the
caller-selected jobject/jclass in r1, with explicit stack, instruction, and
service-call budgets. Return r0 is exposed as raw 32-bit result bits.

General JNI object/reference semantics, pending exceptions, member IDs,
Java-call marshalling, strings, arrays, fields, monitors, direct buffers, and
thread attachment remain outside this delta.
