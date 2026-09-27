# Linked __cxa_finalize and service-aware ELF FINI

Status: accepted; exact-head validation PASSed

The accepted ARM32 partial libc exports `__cxa_finalize` as private SVC
`0xD3`. A controlled consumer FINI_ARRAY entry calls the linked symbol with
its exact guest `__dso_handle` address.

ELF lifecycle execution can optionally dispatch bounded host services. Service
limit, unhandled, failed, and suspended outcomes are explicit and preserve the
failing SVC immediate.

For synchronous guest `__cxa_finalize`, nested registered destructors start
from the trapped caller's live r13 rather than the outer lifecycle's original
stack top. This preserves the active FINI caller frame while nested AAPCS32
callbacks grow below it.

The exact-head revision
`0cd4e1ee1e9d08eb3bde9f50238fd61a5f6af5a6` passed all nine required
checks, including the real forty-one-symbol libc integration.

Still separate: DSO/link-map ownership, last-reference dlclose policy,
dependency ownership/reachability, mapping reclamation, and process-wide
shutdown transactions.
