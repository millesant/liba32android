# Next Work

The numbered 011-049 roadmap is COMPLETE.

The Android filesystem source, persistent legacy lifecycle,
`__aeabi_atexit` registration, registered finalization, guest
`__cxa_finalize`, and service-aware FINI follow-ups are DONE.

No acceptance gate is active.

## Ready next follow-up

Rebase and integrate the prepared resident last-reference dlclose lifecycle
transaction. Bind stable link-map objects to exact guest DSO handles, wire the
transaction into the resident libdl close path, and add focused plus real ARM
regressions. Teardown ordering is reverse FINI_ARRAY / registered finalization /
DT_FINI before releasing the final synthetic handle.

Keep dependency mapping reclamation separate until recursive ownership and
reachability rules are explicit.

## Environment-blocked or decision-blocked work

- Android native tombstone/backtrace coexistence: BLOCKED on accessible device environment.
- AArch64 runtime execution on a 16 KiB Android host: NOT RUN.
- Project license: BLOCKED on maintainer choice.
- JNI/graphics/audio integration requires later application/device-facing work.

## Local-machine validation note

If a follow-up materially requires the user's Linux machine, stop beforehand
and provide exact commands, required inputs, expected output, and why it is
needed.
