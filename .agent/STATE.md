# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `046-a32-libdl-resident-service` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-045 are accepted.

Feature 039 passed all five exact-head checks at
`9160aa24c9f1cebdfe65a79ecc1543a662bec5c4`.

Feature 040 is now integrated. It adds bounded ARM32 `malloc/calloc/realloc/free`
host services at private SVC IDs `0xAE-0xB1` over a caller-provided,
already-mapped writable guest arena and finite caller-owned allocation metadata.

The heap returns only logical 32-bit guest addresses, uses deterministic
16-byte-aligned first-fit placement, publishes Android ENOMEM through the
feature-037 guest errno sink, preserves old allocations on failed realloc
growth, and owns no guest mappings.

The accepted feature-039 Android-17 alignment and feature-033 memmem correction
remain preserved.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 039 at
`9160aa24c9f1cebdfe65a79ecc1543a662bec5c4`:

- Linux A32 smoke `108524978723` — PASS.
- Android arm64-v8a cross-build `108524978738` — PASS.
- Android x86_64 address-space probe `108524978623` — PASS.
- ARM32 liblog shim integration `108524978481` — PASS.
- ARM32 libc memory string shim integration `108524978431` — PASS.

Feature 040 passed all five required exact-head checks at
`84d49653e6d72dca0e116ab2e12342efd4057531`.

Feature 041 is now integrated. It extends the real partial ARM32 `libc.so`
fixture and freestanding consumer with `malloc/calloc/realloc/free` at shared
SVC IDs `0xAE-0xB1`, requires seventeen eager JUMP_SLOT targets, and routes the
four real guest wrappers into one bounded `A32LibcGuestHeap` over a mapped
logical guest arena.

Feature 041 passed all five required exact-head checks at
`330dcdc72e642015fa93f6cd386473c40b438f44`:
- Linux A32 smoke `108561790133` — PASS.
- Android x86_64 address-space probe `108561790131` — PASS.
- Android arm64-v8a cross-build `108561790072` — PASS.
- ARM32 liblog shim integration `108561789771` — PASS.
- ARM32 libc memory string shim integration `108561789680` — PASS.

Feature 042 is now integrated. It adds bounded root-scoped FINI_ARRAY planning
that emits the exact reverse of constructor object order, reverses each
object's FINI_ARRAY entries, preserves sentinel/limit/error semantics, and
executes planned destructors through the existing bounded ARM/Thumb lifecycle
call seam.

Feature 042 passed all five required exact-head checks at
`082edd47b05229c5c3ca0787103a27b7e5bfebce`:
- Linux A32 smoke `108563727432` — PASS.
- Android x86_64 address-space probe `108563727653` — PASS.
- Android arm64-v8a cross-build `108563727607` — PASS.
- ARM32 liblog shim integration `108563727358` — PASS.
- ARM32 libc memory string shim integration `108563727781` — PASS.

## Prepared lineage

Feature 043 is now integrated. It adds bounded plain `memmove` at private SVC
`0xB2` and extends the partial ARM32 `libc.so` from seventeen to thirty
symbols with the twelve bionic ARM EABI memory helpers. The real fixture covers
overlapping memmove, all memcpy/memmove alignment variants, EABI memset
argument reordering, and memclr zeroing.

Feature 043 passed all five required exact-head checks at
`99e79d710db7271557e64e608cf0523c663df169`:
- Android arm64-v8a cross-build `108565884707` — PASS.
- Linux A32 smoke `108565884589` — PASS.
- Android x86_64 address-space probe `108565884447` — PASS.
- ARM32 libc memory string shim integration `108565884330` — PASS.
- ARM32 liblog shim integration `108565884235` — PASS.

Feature 044 is now integrated. It adds a game-agnostic host-service suspension
result and exact bounded resume-request reconstruction so future blocking
pthread/semaphore compatibility can hand control to an external scheduler
without spinning, blocking the host executor, or replaying the trapped SVC.

Feature 044 passed all five required exact-head checks at
`d9e6dcfd7cf49bfc08f40a1f4db4862c7e570fdd`:
- Android x86_64 address-space probe `108567983759` — PASS.
- Linux A32 smoke `108567983727` — PASS.
- Android arm64-v8a cross-build `108567983624` — PASS.
- ARM32 liblog shim integration `108567983255` — PASS.
- ARM32 libc memory string shim integration `108567983214` — PASS.

Feature 045 is now integrated. It adds finite caller-owned pthread mutex and
process-local semaphore state keyed only by logical guest addresses, uses the
feature-044 Suspended seam for contended lock/zero-count wait, transfers grants
before wake publication, and extends the real partial `libc.so` from thirty
to thirty-nine exports with five mutex and four semaphore functions.

Feature 045 passed all five required exact-head checks at
`f03514571104ae0c20c401432674583b5fba3ec9`:
- Android arm64-v8a cross-build `108570667500` — PASS.
- Android x86_64 address-space probe `108570667456` — PASS.
- Linux A32 smoke `108570667328` — PASS.
- ARM32 liblog shim integration `108570666873` — PASS.
- ARM32 libc memory string shim integration `108570666417` — PASS.

Feature 046 is now integrated. It adds a resident-object ARM32 `libdl.so`
compatibility service/shim for `dlopen/dlsym/dlclose/dlerror/dladdr` over the
persistent ELF32 link map. Handles and returned pointers remain logical guest
values; missing-object acquisition, unload, and filesystem/search stay outside
this slice. A dedicated real ARM32 fixture joins namespace-gated `libdl.so`
with an application-resident target DSO and exercises lookup/address/error
paths through guest wrappers.

Feature 046 exact-head validation: NOT RUN.

Prepared order remains:
`047 -> 048 -> 049`.

## Deferred / partial

Legacy DT_INIT/DT_FINI, __aeabi_atexit/static-destructor registration, and
persistent lifecycle state, broader pthread/thread creation/TLS services,
dynamic libdl acquisition/unload semantics, libm, Android filesystem/search,
stable embedding API, and device execution remain
later work.
