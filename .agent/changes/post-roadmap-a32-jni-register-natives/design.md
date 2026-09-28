# Design — ARM32 JNI registration bootstrap

## Table and services

Grow the installed JNINativeInterface region to 216 words. Install distinct
ARM svc/bx-lr stubs for GetEnv, FindClass, and RegisterNatives at
caller-selected aligned guest addresses. The VM service recognizes only those
three private SVC immediates.

## Registry

A32JniClassRegistry is owned by the embedding caller and borrowed by
A32JniVmService. It copies accepted class names and registered method
name/signature metadata into bounded host containers while storing guest
handles/function pointers as uint32_t values only. Class names and handles are
unique.

## Registration transaction

RegisterNatives first validates class identity/count/range, then decodes every
12-byte ARM32 JNINativeMethod and both guest strings under configured ceilings.
Duplicate keys inside one call are rejected. Existing exact bindings may be
updated; new bindings are admitted only if the post-call method count remains
within the registry limit. Mutation occurs only after the complete call has
validated.

## Reverse dispatch

invoke_a32_registered_native_noargs resolves one exact binding and requires a
zero-argument JNI signature. It validates the guest function address, seeds r0
with JNIEnv* and r1 with the caller's receiver/class handle, and reuses
execute_a32_with_services with a caller-owned stack/stop PC and finite
instruction/service budgets. No host pointer is injected into guest state.

## Failure boundary

Guest memory read/write faults remain host-service failures. JNI-level semantic
rejections return null or JNI_ERR where the JNI surface has such a result.
Reverse-dispatch execution errors are classified explicitly like JNI_OnLoad.
