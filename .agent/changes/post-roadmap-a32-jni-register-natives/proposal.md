# Proposal — ARM32 JNI FindClass / RegisterNatives bootstrap

## Intent

Advance the continuous JNI track from VM/GetEnv/JNI_OnLoad into the first
real class/native-registration seam proven by the supplied ARM32 libraries.

## Scope

Publish the real JNIEnv slots used by libmla, add an explicit bounded class
registry, parse ARM32 JNINativeMethod arrays, retain guest native bindings, and
prove one host-to-guest native call after registration.

This slice intentionally stops before general Java objects, member IDs,
references, strings/arrays/exceptions, Java method calls, or framework classes.
