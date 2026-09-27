# Runtime contract

Status: Accepted current project contract
Last reconciled: 2026-09-27

## L32-R001 — Game-agnostic runtime

The generic runtime remains application-agnostic. Application/game-specific behavior belongs under `profiles/` and must not leak into CPU, memory, ELF, ABI, compatibility-library, or platform contracts.

## L32-R002 — Logical guest addresses

AArch32 guest addresses are logical 32-bit values. CPU, memory, ELF, ABI, and runtime APIs must not expose host pointers as guest pointer values.

## L32-R003 — CPU isolation

Dynarmic remains behind `src/cpu/`. Higher layers depend on engine-independent contracts and may not expose Dynarmic types.

## L32-R004 — Guest-memory seam

`memory::GuestMemory` is the generic memory contract. `LinearGuestMemory` is the deterministic correctness/test backend. `MappedGuestMemory` owns mapped guest pages, permissions, and optional high-base 4 GiB fastmem backing.

## L32-R005 — Fastmem is optional

High-base contiguous fastmem is the preferred first acceleration path when available, but callback-backed access remains the correctness fallback. Direct low-VA host-pointer identity is not a correctness requirement.

## L32-R006 — Permission integrity

Runtime layers must not silently broaden guest permissions to make fixtures or compatibility cases pass. Mapping, relocation, and post-relocation hardening remain explicit stages.

## L32-R007 — Shared-library identity

The runtime target produces exactly `liba32android.so`. Android cross-build validation targets `arm64-v8a`; host validation remains separate from Android device/runtime claims.

## L32-R008 — Evidence scope

A passing cross-build, emulator sample, or device sample proves only the stated revision/environment. Broader Android compatibility claims require broader evidence.

## L32-R009 — Resumable A32 SVC state

The engine-independent CPU result may report the exact A32 SVC immediate while retaining the existing generic exception flag for source-compatible callers. Execution requests may optionally seed a full returned CPSR snapshot; when absent, the adapter retains the existing Arm/Thumb user-mode initialization. Returned general registers, logical PC, and CPSR after an SVC are valid input to a follow-up bounded execution request so guest execution can continue after the trap without exposing Dynarmic types. Host-service dispatch, ABI marshalling, compatibility shims, syscall emulation, and Android API behavior remain separate layers.

## L32-R010 — Bounded host-service dispatch

A game-agnostic runtime layer may execute A32 under one total finite guest-instruction budget while synchronously handling feature-023 SVC traps through a caller-owned service handler. The handler receives the exact SVC immediate plus mutable guest registers/CPSR and `memory::GuestMemory`, and returns Handled, Unhandled, or Failed. Successfully handled traps resume from the returned post-SVC logical PC/register/CPSR state while preserving the original stop-PC target. A separate finite service-call ceiling bounds successful handler invocations.

Memory faults, ordinary non-SVC CPU exceptions, unhandled or failed services, service-limit exhaustion, and exhausting a requested stop-PC instruction budget are distinct outcomes. With no stop target, ordinary fixed-budget completion remains successful. Completed handler register/memory effects are not rolled back. The dispatcher performs no guest mapping/protection lifecycle and contains no service registry, ABI marshalling, syscall semantics, Android API behavior, or platform-specific policy.

## L32-R011 — Exact host-service registry

A game-agnostic registry may compose caller-owned `A32HostServiceHandler`
objects by exact SVC immediate. The registry borrows a finite entry span and
never owns, allocates, or reorders handlers. No matching entry returns
`Unhandled`. Exactly one matching non-null child is invoked with the unchanged
`GuestMemory`, SVC immediate, mutable registers, and CPSR, and its disposition
is returned unchanged.

A matching null handler, duplicate matching immediate, or direct self-entry
returns `Failed` before any child handler is invoked. Invalid entries for
unrelated immediates do not affect exact lookup. The registry performs no ABI
argument decoding, platform/API semantics, symbol resolution, shim generation,
syscall emulation, or namespace/search policy.

## L32-R012 — Bounded service suspension and external scheduling

A host-service handler may return `Suspended` when completing the current guest
call requires an external scheduling decision rather than synchronous service
completion. Suspension is a successful terminal dispatch outcome, not a CPU or
service failure. The result preserves the exact post-SVC logical PC, registers,
and CPSR, records the exact suspended SVC immediate, and executes no later guest
instruction in that dispatch call.

A suspended service consumes one entry from the caller's finite service-call
budget. Register and guest-memory effects performed by the handler before
suspension remain visible. `Unhandled` and `Failed` retain their existing
failure semantics.

`make_a32_service_resume_request` may turn only a suspended result into a new
caller-bounded `cpu::ExecutionRequest`. The caller supplies a new finite
instruction budget and optional stop PC; the helper resumes at the logical PC
after the trapped SVC, preserves the exact register/CPSR snapshot, and derives
ARM/Thumb state from CPSR. A zero new budget is rejected.

This contract is a game-agnostic scheduling handoff only. It does not create
host or guest threads, choose runnable-thread order, park on futexes, implement
pthread/semaphore object layouts, or own TLS/errno/heap synchronization.
