# Compatibility spec delta — ARM32 JNI instance int field reads

- Publish `GetIntField` at JNIEnv slot 100 / byte offset `0x190` through one
  distinct private ARM service stub.
- The caller may seed a signed 32-bit value for an exact logical jobject plus an
  existing InstanceField jfieldID.
- Seeding requires a known logical object identity and the exact InstanceField
  member kind; existing pairs update deterministically and new pairs are
  bounded.
- GetIntField requires exact configured JNIEnv/attached state, a currently live
  logical jobject, an existing InstanceField ID, and existing seeded value
  state.
- The jint result is returned bit-for-bit in ARM32 r0.

Object-class assignability, Java field layout/offsets, inheritance, volatile
semantics, SetIntField, reflection, and other field families remain outside
this delta.
