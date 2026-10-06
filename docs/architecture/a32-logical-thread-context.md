# A32 logical thread execution context

Status: accepted current architecture; logical guest-thread context implemented

## Purpose

The runtime already provides engine-independent A32 execution requests,
service-dispatch results, cooperative service suspension, and exact post-SVC
continuations. The logical-thread context seam adds only a reusable identity
for associating those existing execution requests with one logical guest
thread.

It is not a scheduler.

## Identity

`A32LogicalThreadId` is a non-zero 32-bit logical identity. Zero is reserved
for invalid/unselected state. The value is not a host thread ID, host
`pthread_t`, pointer, TLS address, or Dynarmic object.

Compatibility layers may use the same identity to associate their own bounded
per-thread state. Pthread currently uses it for mutex ownership, waiter
identity, and TLS values. Future JNI work may use the same identity for
attachment and thread-local JNI state.

## Execution context

`A32LogicalExecutionContext` pairs one logical thread ID with the existing
`cpu::ExecutionRequest`. It does not copy CPU register/CPSR state into another
representation.

A suspended `A32ServiceDispatchResult` is converted with the existing
`make_a32_service_resume_request` helper. The logical wrapper preserves the
same thread ID while producing the exact post-SVC continuation, so the trapped
service is not replayed.

## Scheduling boundary

The generic seam does not own:

- runnable/blocked/exited lifecycle;
- ready queues or fairness;
- wait reasons or wake ordering;
- pthread join/detach state;
- mutex/semaphore/condition-variable policy;
- TLS destructors;
- JNI attachment/reference/exception semantics.

Those policies remain with the compatibility/runtime orchestration layer that
understands them. Existing pthread waiter queues continue to decide which
logical thread is eligible after a synchronization operation.

## Public ABI

This is an internal C++ contract under `src/runtime/`. It does not add or
change symbols in the public C embedding ABI.
