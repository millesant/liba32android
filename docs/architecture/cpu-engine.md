# CPU engine architecture

Status: current CPU/memory execution architecture

## Boundary

The CPU engine is an internal service. It executes AArch32 instructions and interacts with guest memory only through the generic memory contract. It must not know about ELF dependency resolution, Android APIs, JNI, graphics, audio, Minecraft, or application profiles.

Dependency direction is intentionally one-way:

```text
runtime / loader / ABI layers
          |
          v
 guest address space
          |
          v
   CPU engine adapter
          |
          v
       Dynarmic
```

Application-specific code may depend on the generic runtime, never the reverse.

## Selected engine

The runtime uses Dynarmic, pinned to `azahar-emu/dynarmic` commit `e77b1ba0b7da7cbe93021b01a663acfe7c4dd516` (2026-06-24).

The selection is based on directly observed upstream properties:

- A32 guest frontend including ARMv7 and Thumb/Thumb-2 families.
- AArch64 host backend.
- Android listed as a supported host OS.
- Bring-your-own-memory callbacks plus page-table and 4 GiB fastmem hooks.
- Cache invalidation API and configurable code cache.
- C++20 embedding API.
- 0BSD license for Dynarmic itself.

## CPU adapter

`src/cpu/a32_cpu.h` is the generic execution seam. Callers provide an instruction set, entry PC, initial A32 registers and a bounded instruction count. Results contain final registers/CPSR plus exception and memory-fault state.

`src/cpu/dynarmic_cpu.cpp` owns the Dynarmic-specific `UserCallbacks` implementation. Dynarmic types do not appear in the generic CPU or memory APIs.

The execution result currently also carries internal diagnostics (`fastmem_enabled` and callback counters) used by regression/device-smoke tests to prove which memory path actually ran. These fields are not a public runtime ABI.


## Persistent execution sessions

`cpu::A32Executor` is the reusable engine-independent CPU session. It owns one
Dynarmic JIT internally and may survive across bounded execution slices and
host-service traps so translated code can remain cached. The legacy free
`cpu::execute(GuestMemory&, ...)` function remains a one-shot convenience
wrapper and constructs a temporary executor.

A reused executor restores caller-supplied core registers/CPSR for every slice,
zeros the same extended-register state as the previous one-shot path, and clears
Dynarmic's exclusive-monitor state between slices. Translation-cache lifetime
therefore changes without letting per-core exclusive state leak between logical
execution contexts.

Exact guest-instruction budgets remain authoritative. The pinned Dynarmic
`Jit::Run()` contract may retire more instructions than a requested tick
budget, so bounded execution continues to use exact `Jit::Step()` retirement.
Persistent JIT lifetime is an independent optimization and does not weaken the
instruction-count or stop-PC contract.

`GuestMemory::code_generation()` is the engine-independent invalidation token
for changes that can affect executable bytes or execute eligibility. A backend
that cannot provide a token returns `nullopt`, which makes a persistent CPU
session conservatively clear its translation cache between execution calls.
`LinearGuestMemory` advances its token on every non-empty write.
`MappedGuestMemory` advances it when executable mappings/permissions change
or an ordinary guest-memory write touches executable pages.

Fastmem writes bypass `GuestMemory::write()`. For writable+executable mapped
pages, `MappedGuestMemory::direct_executable_writes_possible()` therefore
forces conservative cache clearing between exact instruction steps. Normal
ELF-like RX code plus RW data retains the persistent translation cache and the
direct fastmem data path. No host pointer becomes a guest-visible address.

## Guest-memory seam

`src/memory/guest_memory.h` defines `memory::GuestMemory`, the engine-independent memory contract. It separates data reads/writes from instruction reads, exposes an optional internal `fastmem_base()` capability, and provides bulk byte primitives for move/copy, fill, compare, and byte search. Those bulk operations have generic callback-backed fallbacks, while `LinearGuestMemory` and `MappedGuestMemory` override them with allocation-free direct implementations after the same logical range and permission checks. Higher runtime layers continue to traffic only in logical 32-bit guest virtual addresses; neither the host reservation pointer nor backend-local spans leak into loader/ABI APIs.

Two implementations currently exist:

- `LinearGuestMemory`: bounded vector-backed correctness/test implementation; no fastmem capability.
- `MappedGuestMemory`: sparse logical 32-bit address space backed by one contiguous 4 GiB host reservation.

`MappedGuestMemory` owns page mapping metadata and page-aligned `map`, `protect`, and `unmap` lifecycle operations. Unmapped pages remain `PROT_NONE`. Mapped pages are made host-accessible with `mprotect`, while guest read/write/execute permission checks remain explicit in the generic memory API. Unmap discards anonymous page contents and returns the page to `PROT_NONE` without giving up the enclosing 4 GiB reservation.

The first mapped backend intentionally accepts the normal ELF-like permission shapes `R`, `RW`, and `RX` (plus `None`) and rejects write-only/execute-only mappings. This keeps guest permission metadata compatible with the direct fastmem host protection used by this first implementation; broader permission emulation can be added if a real binary requires it.

Bulk writes use the same executable-code invalidation contract as ordinary writes. `MappedGuestMemory` validates the entire source/destination range before a direct move or fill, advances `code_generation()` when an executable destination is changed, and retains ordinary read/write permission failure semantics. Callback-only backends remain correct through the base implementations.

## Fastmem integration

D-0004 selects a high-base contiguous 4 GiB reservation as the preferred first Android acceleration path, based on observed Android/AArch64 device evidence. Low host virtual addresses are not required.

When a `GuestMemory` implementation exposes `fastmem_base()`, the Dynarmic adapter sets `UserConfig::fastmem_pointer` to that reservation base and keeps `recompile_on_fastmem_failure=true`. Therefore a guest address `G` is represented by the host address `fastmem_base + G` for fast-path data accesses, while the guest-visible value remains the 32-bit `G`.

If a fastmem access hits a protected/unmapped host page, Dynarmic may recompile the block with fastmem disabled and route the access through the ordinary `GuestMemory` callbacks. The callback path remains mandatory correctness behavior, not a separate API.

Linux regression coverage proves both sides of this seam:

- callback-only `LinearGuestMemory` executes A32 load/store through data callbacks;
- `MappedGuestMemory` executes the same mapped A32 load/store with fastmem enabled and no data callbacks;
- an A32 load from an unmapped fastmem page falls back to the callback path and surfaces `memory_fault`.

Real Android/AArch64 runtime-smoke evidence from 2026-09-16 proves the tested direct and fallback paths on one Android 16 / runtime SDK 36 Termux environment:

- A32 `mov r0,#42` executed through the pinned Dynarmic AArch64 backend and returned 42;
- mapped A32 `STR`/`LDR` produced the expected `0x12345678` value;
- both mapped data callback counters were zero and `a32.memory.fastmem_direct=true`, directly supporting use of the configured fastmem data path;
- an A32 load from an unmapped guest page reported `memory_fault=true`, invoked one data-read callback, reported `a32.fastmem_fault.status=PASS`, and the process still reached `runtime_smoke.complete=true`.

The final point demonstrates the intended fastmem fault -> callback correctness fallback on that environment rather than a fatal host-process crash.

## Android runtime smoke

`android_runtime_smoke` is an Android arm64 diagnostic executable linked to the real `liba32android.so`. It is packaged with the shared library and a Termux-oriented launcher that sets `LD_LIBRARY_PATH` to the bundle directory.

The device smoke has now demonstrated on one real Android/AArch64 environment:

1. creation of `MappedGuestMemory` and its 4 GiB reservation;
2. A32 `mov r0,#42` execution through Dynarmic's AArch64 backend;
3. A32 `STR`/`LDR` through mapped fastmem with zero data callbacks;
4. fastmem fault -> callback fallback via `--exercise-fastmem-fault`, surfacing a guest memory fault without terminating the process;
5. explicit `--crash-test` SIGABRT handling, emitting the `A32CRASH` marker before fatal process termination.

Raw evidence and the Observed/Inferred/Not-demonstrated classifications live under `docs/research/evidence/android-runtime-smoke-termux-arm64-2026-09-16.*`, `docs/research/evidence/android-runtime-smoke-fastmem-fallback-termux-arm64-2026-09-16.*`, and `docs/research/evidence/android-runtime-smoke-crash-test-termux-arm64-2026-09-19.*`.

## Exact stop-PC execution

`ExecutionRequest::stop_pc` is an optional normalized logical guest PC. The
Dynarmic adapter checks it before the first instruction and after every stepped
instruction. When reached, execution stops before fetching from that address
and `ExecutionResult::stop_pc_reached` records the terminal condition.

This is additive: callers that omit `stop_pc` retain the existing fixed
instruction-count semantics. The stop target need not be mapped, which lets
bounded guest calls return through LR without staging executable sentinel code.
Exceptions and memory faults remain explicit terminal states.

Feature 019 uses this seam for INIT_ARRAY constructors; the CPU layer remains
unaware of ELF or lifecycle policy.

Feature 019 result revision `28a4f92f78b8ff156ee9dd3083d1218af8b7b125`
PASSed exact-head Linux A32 smoke and both required Android checks. The CPU
regressions specifically prove ARM/Thumb return to an unmapped stop PC,
initial stop-before-fetch, and stop arrival on the final permitted instruction.

## Resumable SVC state

Feature 023 extends the generic CPU seam with two additive pieces of state.

`ExecutionResult::svc_immediate` reports the exact immediate supplied by
Dynarmic's A32 SVC callback. SVC continues to set `exception_raised=true`, so
existing callers that only treat SVC as a generic exception retain their
behavior. Ordinary exceptions leave `svc_immediate` empty.

`ExecutionRequest::initial_cpsr` optionally seeds an exact caller-owned CPSR
snapshot. When absent, the adapter retains the existing ARMv7 user-mode
initialization derived from `instruction_set`. When present, the snapshot is
used directly, allowing callers to feed back a previous execution result.

After an SVC, returned general registers, `regs[15]`, and CPSR describe the
post-instruction continuation point. Focused ARM and Thumb coverage traps on
nonzero immediates, reconstructs a follow-up request from that returned state,
and executes the next guest instruction. The Thumb resume deliberately leaves
`instruction_set` at its default ARM value, proving the returned CPSR T bit
restores Thumb state.

This layer still does not dispatch host services, decode AAPCS arguments,
implement syscalls, or know about ELF/platform shims.

## Feature 023 validation

Result revision `4b6234232cef70655272b0877f6c8ae971236a77`
PASSed Linux A32 smoke check `108375486343`, Android x86_64
address-space probe check `108375486470`, and Android arm64-v8a cross-build
check `108375486484`.

The Linux CPU suite proves exact ARM and Thumb SVC immediates, post-SVC
continuation PC/state, and successful execution after reconstructing a request
from returned registers/CPSR. The Thumb resume deliberately leaves the request's
instruction-set selector at its default, proving the returned CPSR T bit drives
the resumed state.

## Correctness policy

Dynarmic documents known accuracy tradeoffs and is not treated as a formal ARM reference implementation. Tiny regression binaries and, where practical, a slower reference path will be used to verify runtime behavior. Unsupported behavior must be surfaced rather than silently declared compatible.

The current device evidence is deliberately environment-specific. It proves these tested paths on one Android 16 / SDK 36 AArch64 Termux process; it does not establish broad device compatibility or broad A32 ISA completeness.

## Sources

- https://github.com/azahar-emu/dynarmic/tree/e77b1ba0b7da7cbe93021b01a663acfe7c4dd516
- https://github.com/azahar-emu/dynarmic/blob/e77b1ba0b7da7cbe93021b01a663acfe7c4dd516/README.md
- https://github.com/azahar-emu/dynarmic/blob/e77b1ba0b7da7cbe93021b01a663acfe7c4dd516/src/dynarmic/interface/A32/config.h
- https://github.com/azahar-emu/dynarmic/blob/e77b1ba0b7da7cbe93021b01a663acfe7c4dd516/src/dynarmic/interface/A32/a32.h
