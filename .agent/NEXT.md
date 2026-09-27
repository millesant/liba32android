# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-resident-dlclose-lifecycle-transaction` is DONE. Its validated
result revision is `efa2ce78e7d77ddf9c92ee29cdbe5a1f03fc6dd2`.

Next, model recursive dependency ownership/reachability and safe object
reclamation. Define the ownership/reference invariants first, including shared
dependencies and cycles, then determine when an object becomes unreachable from
all live roots/handles. Only after those rules are explicit should persistent
link-map entries be removed or guest mappings be unmapped.

Keep RTLD_NODELETE/global-group policy and dynamic missing-object dlopen separate
unless the ownership model proves they must be represented at the same seam.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
