# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-targeted-dlclose-unload` is DONE at
`742590cbc6740c9467e4b9d5f04884deeda16c1e`.

`post-roadmap-aeabi-dso-binding-association` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

Constructor/destructor lifecycle calls now expose scoped stable-object
provenance. `__aeabi_atexit` can learn bounded object-to-DSO associations from
real guest registrations, dynamic-open constructors preserve that context,
libdl close consumes learned bindings, and successful physical unload retires
learned association state.

After terminal success, model libdl load-policy semantics explicitly:
RTLD_LOCAL versus RTLD_GLOBAL visibility, RTLD_NODELETE retention, and a
bounded resident RTLD_NOLOAD query. Keep the existing eager RTLD_NOW behavior;
do not pretend RTLD_LAZY is implemented merely because the ABI flag is
accepted.

The policy layer must compose with current persistent roots/global scope,
synthetic-handle ownership, targeted final close, and tombstone reclamation.
RTLD_NODELETE should block physical retirement without turning visibility into
ownership accidentally; RTLD_GLOBAL promotion must be deterministic and
persistent while the object remains Active.

Keep RTLD_NEXT, concrete Android pathname/APK search policy, tombstone
compaction, process-wide exit, and JNI/graphics/audio separate.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
