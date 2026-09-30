# Compatibility spec delta — ARM32 JNI raw CallStaticVoidMethod

- Publish `CallStaticVoidMethod` at JNIEnv slot 141 / byte offset `0x234`
  through one distinct private ARM service stub.
- Require exact configured JNIEnv/attached state, a live exact logical jclass,
  and an existing StaticMethod jmethodID belonging to that class.
- Decode the raw variadic argument stream using the accepted ARM32 r3-plus-stack
  descriptor/AAPCS32 decoder.
- Require every non-null decoded reference argument to be a live logical JNI
  identity.
- Invoke a caller-owned synchronous static-void method bridge and return void
  only when that bridge accepts the call.

CallStaticVoidMethodV/A, return-valued static calls, Java class initialization,
dispatch/inheritance, and framework behavior remain outside this delta.
