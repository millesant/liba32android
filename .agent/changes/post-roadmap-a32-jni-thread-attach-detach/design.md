# Design — ARM32 JNI thread attachment

## Evidence-backed JavaVM entries

Supplied ARMv7 libmla.so machine code loads:

- AttachCurrentThread from JavaVM byte offset 0x10, slot 4;
- DetachCurrentThread from byte offset 0x14, slot 5;
- GetEnv from byte offset 0x18, slot 6.

The existing eight-word JavaVM invocation table already spans these positions.

## State model

A32JniVmService continues to model one bounded guest JNI context. install()
starts that context attached to preserve existing JNI_OnLoad behavior.

AttachCurrentThread requires the exact configured JavaVM pointer and a writable
JNIEnv** output slot. The attach-args pointer is accepted but not interpreted in
this slice. Success writes the configured logical JNIEnv pointer, marks the
context attached, and returns JNI_OK. Re-attaching the already-attached context
is idempotent.

DetachCurrentThread requires the exact JavaVM pointer. The first detach marks the
context detached and returns JNI_OK. Detaching an already-detached context
returns JNI_ERR.

GetEnv keeps version validation first. For a supported version while detached it
returns JNI_EDETACHED without touching the output slot. After reattachment it
again writes the configured logical JNIEnv pointer and returns JNI_OK.

JNIEnv-native services fail while detached.

## Deliberate boundary

This is a single-context compatibility model, not a host-thread registry.
AttachCurrentThreadAsDaemon remains null, JavaVMAttachArgs is opaque, and no
thread-local Java object/reference model is introduced here.
