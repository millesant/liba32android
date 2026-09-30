# Compatibility spec delta — ARM32 JNI_OnUnload

A caller may explicitly invoke `JNI_OnUnload` for one exact already-loaded
dependency-graph object.

The helper resolves only the target object's own `JNI_OnUnload` STT_FUNC and
executes it as `void JNI_OnUnload(JavaVM*, void*)` with r0 set to the exact
configured JavaVM logical guest address and r1 set to null. ARM/Thumb entrypoint
rules, stack alignment, instruction ceilings, service ceilings, and optional
object lifecycle provenance match the existing bounded JNI_OnLoad execution
contract.

Missing exact-object symbols, invalid objects/options/entrypoints, memory or CPU
faults, service failures/suspension, and instruction/service budget exhaustion
are explicit failures. There is no returned-version validation because the
entrypoint is void.

Automatic final-close/class-loader/process-exit invocation and teardown ordering
remain outside this delta.
