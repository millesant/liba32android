# Linked __cxa_finalize and service-aware ELF FINI

Status: implemented; exact-head validation pending

## Goal

Connect the accepted guest `__cxa_finalize` service to ordinary linked ARM32
code and to the ELF FINI executor, matching the Android shared-object teardown
shape without yet owning unload.

## Partial libc export

The generated partial `libc.so` adds `__cxa_finalize` as SVC `0xD3`.
Its real freestanding consumer imports the symbol normally, expanding the
target-backed surface from forty to forty-one eager JUMP_SLOT targets.

The consumer contains one controlled FINI_ARRAY entry. That function calls
`__cxa_finalize(&fixture_dso_handle)`. The integration previously registers a
guest destructor for that exact DSO word through `__aeabi_atexit`.

## Service-aware lifecycle calls

`Elf32InitExecutionOptions` now has an optional borrowed host-service handler
and finite maximum service calls per lifecycle function. The same execution
options back FINI and persistent lifecycle calls.

Without a handler, behavior is unchanged. With a handler, lifecycle code runs
through the bounded A32 service dispatcher. Service limit, unhandled, failed,
and suspended outcomes are explicit lifecycle errors; the failing SVC immediate
is preserved where available.

When guest `__cxa_finalize` synchronously invokes registered guest destructors,
the finalizer starts those nested callbacks at the trapped caller's live r13.
It must not restart them at the outer lifecycle's original stack top: the FINI
function still owns an active frame there, and nested callback prologues could
overwrite its saved return state.

## End-to-end FINI path

The ARM32 integration performs:

`FINI_ARRAY entry -> linked __cxa_finalize JUMP_SLOT -> libc.so SVC 0xD3 -> registered finalizer -> guest destructor -> resume FINI -> return`

The registered destructor receives the exact object word in r0 and writes a
known marker. Its record becomes Complete. A later direct wrapper call to
`__cxa_finalize` succeeds without replaying it.

## Limits

DSO-handle/link-map ownership, last-reference dlclose policy, registered
callback service dispatch, mapping reclamation, and process-wide shutdown
transactions remain separate.
