# Delta spec — ARM32 JNI CallVoidMethodV bridge

- Publish JNIEnv CallVoidMethodV at slot 62 / byte offset `0xf8` only.
- Use a distinct private ARM `svc; bx lr` stub and leave unsupported native
  table slots, including raw variadic CallVoidMethod slot 61, null.
- Require exact JNIEnv, attached state, live logical receiver, existing
  InstanceMethod member ID, a void-return descriptor, and a caller-owned bridge.
- Bound decoded argument count by configurable and hard ceilings.
- Decode Z/B/C/S/I as 32-bit promoted words; J/D as AAPCS32 8-byte-aligned
  little-endian 64-bit values; F as default-promoted double narrowed to jfloat;
  object/array descriptors as 32-bit logical references.
- Require each non-null reference argument to be currently live.
- Copy member metadata before one synchronous bridge callback and expose only
  typed logical values, never host pointers.
- Fail deterministically on malformed descriptors, invalid logical state,
  unreadable guest argument memory, missing bridge, or rejected callback.
- Keep raw/A-form calls, other Call families, NewObject, inheritance/dispatch,
  and framework Java behavior outside this delta.
