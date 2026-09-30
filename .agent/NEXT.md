# NEXT

## Active JNI track

Continue `post-roadmap-a32-jni-new-object-v` from validated exception-state
revision `629566d175f03d209d0f90ed702fda580c98f1e1`.

Supplied ARMv7 `libmla.so`
(`sha256:4ad00d934ea348d535ef35581c41d778abd4919790e32a8c8fc741b22db08962`)
contains a weak C++ `_JNIEnv::NewObject(...)` wrapper that constructs a
`va_list` and loads JNIEnv offset `0x74`, slot 29 (`NewObjectV`).

Keep this slice bounded to exact slot 29, existing constructor method metadata,
the accepted descriptor/`va_list` decoder, and a caller-owned fresh logical
jobject result with one local reference. Leave raw NewObject/NewObjectA, Java
heap layout, inheritance/assignability, and constructor bytecode separate.

## Validation

Run focused host coverage first, then exact-head checks. After terminal success,
leave CI and converge evidence/state.
