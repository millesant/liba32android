# Design — ARM32 JNI_OnUnload invocation

## Evidence boundary

The supplied VLC ARMv7 artifacts export real unload entrypoints:

- `libmla.so` (SHA-256
  `4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`)
  exports Thumb `JNI_OnUnload` at symbol value `0x002614d1` (132 bytes).
  Its body obtains JNIEnv for JNI 1.2 before releasing JNI-owned state.
- `libvlcjni.so` (SHA-256
  `e76e20218203548bb88f5ef39a9aad6b2fe8efcbe27175e6fcc013b5c779b816`)
  exports Thumb `JNI_OnUnload` at `0x00007b5d` (500 bytes). It calls
  JavaVM::GetEnv through invoke-table offset `0x18`, then repeatedly calls
  JNIEnv::DeleteGlobalRef through offset `0x58`.

Those operations are already supported by the bounded JavaVM/JNIEnv services,
so the missing seam is entrypoint execution rather than a new JNI table slot.

## Invocation contract

`invoke_a32_jni_on_unload` mirrors the accepted exact-object JNI_OnLoad
lookup/execution boundary:

- build the target object's bounded symbol index;
- resolve `JNI_OnUnload` only in that exact object;
- require a non-null STT_FUNC ARM/Thumb entrypoint;
- execute with r0=JavaVM*, r1=null, caller-owned stack and stop PC;
- preserve exact object lifecycle provenance during nested guest services;
- enforce caller-provided instruction and service-call ceilings.

JNI_OnUnload returns void, so successful stop-PC completion has no return-value
validation.

## Boundary

This slice is explicit invocation only. It does not automatically call
JNI_OnUnload from dlclose, class-loader collection, process exit, or generic DSO
teardown. That ordering/policy remains a separate lifecycle integration change.
