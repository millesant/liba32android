# Compatibility spec delta — ARM32 JNI raw CallVoidMethod

- Publish raw variadic `CallVoidMethod` at JNIEnv slot 61 / byte offset
  `0xf4` through one distinct private ARM service stub.
- Preserve the accepted `CallVoidMethodV` slot 62 behavior.
- Require exact JNIEnv/attached state, a live logical receiver, an existing
  InstanceMethod ID, a structurally valid void-return descriptor, and a
  caller-owned method-call bridge.
- Decode the first promoted 32-bit raw variadic argument from r3 and subsequent
  32-bit arguments from the guest stack.
- For jlong, jdouble, and C-default-promoted jfloat, do not split a 64-bit
  value across odd r3 and the stack. Advance to an 8-byte-aligned guest stack
  address and consume two little-endian words.
- Decode object/array descriptors as logical reference handles and require
  every non-null decoded reference to be currently live.
- Normalize decoded values into the same typed synchronous bridge vector used
  by CallVoidMethodV.

CallVoidMethodA, return-valued/static/nonvirtual call families, NewObject,
inheritance/virtual dispatch, and Java framework behavior remain outside this
delta.
