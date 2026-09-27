# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `042-elf32-fini-lifecycle` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-041 are accepted.

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

Feature 042 exact-head validation: NOT RUN.

## Prepared lineage

Off-ref work remains prepared through feature 049:
`043 -> 044 -> 045 -> 046 -> 047 -> 048 -> 049`.

## Deferred / partial

Legacy DT_INIT/DT_FINI and persistent lifecycle state,
ARM EABI memory helpers, pthread/semaphore scheduling boundaries, libdl, libm,
Android filesystem/search, stable embedding API, and device execution remain
later work.
