# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-call-void-method-v` from validated ThrowNew
revision `361b9ffb044d5ed4a6cdfa080f3e93bec7893c9d`.

The supplied VLC ARMv7 `libmla.so` proves that its C++ variadic
`_JNIEnv::CallVoidMethod(jobject, jmethodID, ...)` wrapper loads JNIEnv native
slot 62 / byte offset `0xf8`, i.e. `CallVoidMethodV`, and forwards an ARM32
`va_list` pointer in `r3`.

Keep this slice bounded to:

- publish only the evidence-backed `CallVoidMethodV` table entry;
- validate a live receiver and an existing instance-method ID;
- decode bounded JNI descriptors from the guest ARM32 `va_list`, including
  AAPCS32 8-byte alignment and C default promotion of `jfloat` to `double`;
- normalize decoded primitive/reference values into one host-call vector;
- delegate Java-side behavior through a caller-owned method-call bridge;
- keep raw variadic slot 61, NewObject, return-valued/static/nonvirtual method
  families, inheritance/dispatch, and a general Java object runtime separate.

## Validation

After the implementation is committed, use exact-head checks first. Do not
claim the new slice verified until all required checks for that exact head
conclude success.
