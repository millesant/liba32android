# ARM32 JNI CallVoidMethodV bridge design

## Evidence boundary

The supplied VLC ARMv7 `libmla.so` contains a C++ `_JNIEnv::CallVoidMethod`
wrapper that builds an ARM32 `va_list`, loads the JNIEnv function pointer at
byte offset `0xf8` (slot 62), and calls it with JNIEnv, receiver, method ID,
and `va_list` in r0-r3. This slice therefore publishes CallVoidMethodV only.
Slot 61 and the A-form remain null.

## Logical call values

The existing member registry remains the source of opaque `jmethodID`
identity. A CallVoidMethodV transaction requires a live receiver and an
InstanceMethod member. The member descriptor must be structurally valid and
return void.

Guest varargs are normalized into `A32JniValue` records with an explicit kind
and raw logical bits. Z/B/C/S/I consume one 32-bit promoted word. J and D align
the ARM32 va cursor to 8 bytes and consume two little-endian words. F consumes
the default-promoted double and is narrowed to jfloat bits. Object and array
descriptors consume one logical 32-bit reference; non-null values must already
be live in the reference ledger.

Argument count is explicitly hard/config bounded. Descriptor parsing never
walks beyond the accepted signature string.

## Embedding boundary

`A32JniMethodCallBridge` is caller-owned and borrowed by `A32JniVmService`.
The service copies method metadata before calling the bridge so a synchronous
embedding callback cannot invalidate a registry vector reference. Argument
storage is borrowed only for the duration of the callback.

The bridge decides Java-side behavior. The compatibility layer does not create
host Java objects, infer receiver classes, perform inheritance lookup, or
fabricate framework methods.

## Failure model

Wrong JNIEnv/attachment, dead receiver, missing/wrong-kind method ID, malformed
descriptor, over-limit argument count, unreadable guest va_list memory, dead
non-null reference argument, missing bridge, or bridge rejection yields a
failed host-service transaction. Successful CallVoidMethodV has no JNI return
value and clears the service return register to zero.

## Deferred

Raw variadic CallVoidMethod, CallVoidMethodA, return-valued/static/nonvirtual
Call families, NewObject[A/V], Java inheritance/virtual dispatch, Java frames,
and framework behavior remain separate evidence-driven slices.
