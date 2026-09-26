# Current State

Last updated: 2026-09-26
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `033-a32-libc-copy-search-service` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-032 are accepted.

Feature 032 passed the full exact-head matrix at
`b4dffe4d7aa429c1dd6bf833f53c0ca7dda70531`, including the dedicated real
ARM32 partial-libc fixture/integration check.

Feature 033 is now integrated. It extends `A32LibcMemoryStringService` with
bounded `memmem`, `strcpy`, and `strncpy` at private SVC IDs
`0xA8/0xA9/0xAA`, justified by corrected supplied-target import evidence.

The feature remains host-service only; the matching guest-shim export extension
is feature 034.

## Validation and repository-truth evidence

Latest accepted behavior-changing result: feature 032 at
`b4dffe4d7aa429c1dd6bf833f53c0ca7dda70531`:

- Linux A32 smoke `108508593847` — PASS.
- Android x86_64 address-space probe `108508593996` — PASS.
- Android arm64-v8a cross-build `108508594058` — PASS.
- ARM32 liblog shim integration `108508593573` — PASS.
- ARM32 libc memory string shim integration `108508593648` — PASS.

Feature 033 exact-head validation attempt at `65fa72edaf22e652df243b9f69b7cc5c71437aeb`: FAILED Linux A32 smoke check `108510459127`; the other four required checks passed. The failing CTest was `a32_libc_string_copy_search_service`. Source inspection localized the mismatch to memmem: the test requires short-haystack/empty-needle result-only paths to avoid guest-range validation, but the implementation validated ranges first. A corrective exact-head revision reorders only those no-read decisions ahead of range validation.

## Deferred / partial

The matching copy/search guest-shim exports, integer/errno, allocator, lifecycle
registration/finalization, pthread/TLS, I/O/stdio/socket, libdl, libm, Android
filesystem/search policy, and device execution remain later work.
