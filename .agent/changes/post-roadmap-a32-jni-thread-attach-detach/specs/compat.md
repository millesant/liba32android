# Compatibility spec delta — ARM32 JNI thread attachment

The JavaVM invocation table publishes:

- AttachCurrentThread at slot 4 / byte offset 0x10;
- DetachCurrentThread at slot 5 / byte offset 0x14;
- existing GetEnv at slot 6 / byte offset 0x18.

Each new entry targets a distinct private ARM service stub.

The VM service models one bounded attached/detached JNI context. Installation
begins attached. AttachCurrentThread requires the exact logical JavaVM pointer
and writable JNIEnv** output, writes the configured logical JNIEnv pointer,
marks the context attached, and returns JNI_OK. The attach-args pointer is not
interpreted. DetachCurrentThread returns JNI_OK when transitioning from attached
to detached and JNI_ERR when already detached.

GetEnv keeps version validation ahead of output access. A supported version on a
detached context returns JNI_EDETACHED without modifying *env. JNIEnv-native
services fail while detached.

This slice does not implement multiple host threads, JavaVMAttachArgs semantics,
AttachCurrentThreadAsDaemon, Java thread objects, or thread-local reference
lifetime.
