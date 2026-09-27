# A32 host-service dispatch

Status: feature 024 complete; feature 044 service-suspension extension implemented, exact-head validation pending

## Boundary

`src/runtime/a32_service_dispatch.*` is a game-agnostic orchestration layer
above the engine-independent CPU and GuestMemory contracts. It knows neither
Dynarmic nor Android APIs.

The caller owns an `A32HostServiceHandler`. For each SVC trap the dispatcher
passes:

- `memory::GuestMemory&`;
- the exact SVC immediate reported by feature 023;
- mutable A32 general registers, including the logical guest PC;
- mutable CPSR.

The handler returns `Handled`, `Unhandled`, `Failed`, or `Suspended`. No service-number
registry or ABI interpretation exists in this layer.

## Bounded loop

`execute_a32_with_services` takes an initial
`cpu::ExecutionRequest`. Its `instruction_count` is one total budget shared
across every CPU resumption. A separate `max_service_calls` bounds successful
host-service invocations.

Each CPU slice runs with the remaining instruction budget. After a handled SVC,
the next slice is reconstructed from the returned/mutated registers, logical
PC, and CPSR. The original stop-PC target is preserved. This means ARM and
Thumb service stubs can trap to the host and then continue through ordinary
guest instructions such as `bx lr`.

A handled service on the final permitted guest instruction is still delivered
to the handler. With no stop target, that is a successful fixed-budget
completion. With a requested stop target that has not been reached, the result
is instruction-limit exhaustion.

## Failure semantics

The runtime result distinguishes:

- guest memory fault;
- ordinary CPU exception without SVC;
- handled-service ceiling exhaustion;
- unhandled service;
- failed service;
- total instruction budget exhausted before a requested stop PC.

When a service-specific error occurs, the failing SVC immediate is retained.
Final registers/CPSR, total guest instructions, successful service count, and
stop state are always returned.

The dispatcher is not a transaction. Register or guest-memory changes from
completed handlers remain visible if a later service or CPU step fails. It
never maps, protects, unmaps, or broadens guest permissions.

## Validation scope

Synthetic runtime coverage exercises ARM and Thumb service programs that trap,
let the handler mutate return registers/guest memory, resume in the correct
instruction set, and return to an unmapped logical stop PC. Additional cases
cover service ceilings, unhandled/failed handlers, completed-side-effect
preservation, memory faults, non-SVC CPU exceptions, requested-stop instruction
exhaustion, and no-stop fixed-budget compatibility.

No Android service implementation, AAPCS stack marshalling, shim ELF, syscall
ABI, or namespace/provider policy is claimed by this feature.


## Feature 024 validation

Result revision `d4e7b480e28d13e8edc5dd1ccf28abbc3072a7a1`
PASSed Linux A32 smoke check `108377362582`, Android x86_64
address-space probe check `108377362552`, and Android arm64-v8a cross-build
check `108377362391`.

The Linux runtime suite covers ARM/Thumb service resume, exact service IDs,
handler register/memory mutation, service limits, unhandled/failed handlers,
preservation of completed handler side effects, CPU faults/exceptions,
requested-stop budget exhaustion, and no-stop fixed-budget completion.

## Feature 044 external scheduling boundary

Feature 044 adds one fourth host-service disposition: `Suspended`. It is for
guest operations whose synchronous fast path cannot complete until some
external scheduler changes runnable state — for example a future contended
pthread mutex, semaphore wait, condition wait, or thread join.

When a handler returns `Suspended`, the dispatcher counts that delivered
service against the finite service ceiling and returns immediately with no
error. The result identifies the exact SVC and carries the post-SVC register,
logical-PC, and CPSR snapshot. No instruction after the SVC executes before the
caller gets control back, and completed handler memory/register effects are not
rolled back.

`make_a32_service_resume_request` turns that terminal snapshot into a new
independently bounded execution request after the embedding decides the guest
thread may run again. It restores every register and CPSR bit, derives ARM vs
Thumb from CPSR, starts at the logical PC already advanced past SVC, and uses a
new caller-selected finite instruction budget and optional stop PC. This avoids
replaying the blocking service on wake.

The runtime still owns no scheduler, host thread, futex wait, pthread object,
TLS selection, or synchronization policy. Feature 044 establishes the
game-agnostic suspend/resume seam required before pthread/semaphore compatibility
can safely model blocking instead of spinning or blocking the host executor.
