# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-dynamic-dlopen-acquisition` is DONE at
`40479ffebe61c22c90d7d523292fc9e787be635d`.

`post-roadmap-targeted-dlclose-unload` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The final-close path now computes a clean post-root-release ownership plan,
preserves shared roots/live handles, finalizes only newly unreachable objects
in requester-before-dependency order, retries without replaying Complete
teardown, physically releases/reclaims only after lifecycle success, and clears
the final synthetic handle last.

After terminal success, address automatic dynamic object-to-DSO-handle
association so provider-acquired objects can participate in registered
`__aeabi_atexit` / `__cxa_finalize` teardown without caller-maintained static
bindings.

Do not guess a DSO handle from load bias or a possibly absent dynamic symbol.
Prefer an explicit execution-context seam that can associate registrations made
while a known persistent object constructor is executing, or another
evidence-backed Android-compatible mechanism.

Keep RTLD_NODELETE/global-group policy, RTLD_GLOBAL/LOCAL expansion, RTLD_NEXT,
lazy binding, pathname/APK policy, and tombstone compaction separate unless
that binding work proves one must share the same contract.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
