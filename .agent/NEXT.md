# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-link-map-reclamation-transaction` is DONE. Its validated result
revision is `33037680cdf3dd25a7b60dc12b051fef5cfebf98`.

`post-roadmap-dynamic-dlopen-acquisition` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

Named libdl acquisition can now resolve a missing root through the caller-owned
provider seam, append/reuse it as a Local persistent root, eagerly relocate
newly appended objects with current global scope, seal GNU RELRO, execute
persistent constructors with trapped live guest r13, and publish a synthetic
handle only after successful initialization.

Pre-constructor failures remove a root added by that attempt and physically
reclaim newly unreachable Pending/Pending mappings while preserving existing
live-handle ownership. Constructor-stage failures remain resident with latched
Failed state and are never replayed. Retired tombstones are excluded from
resident libdl lookup/address behavior.

After terminal success, implement targeted final-close unload lifecycle over
only objects that become unreachable after releasing dynamic ownership. Compute
post-release reachability first, preserve shared dependencies that remain owned,
run registered/ELF teardown only for the newly unreachable set in deterministic
requester-before-dependency order, then release ownership and invoke the
accepted physical reclamation transaction.

Do not implement final-close by calling the existing whole-root persistent
destructor traversal: that would incorrectly destroy shared dependencies that
remain reachable from another root or live handle.

Keep RTLD_NODELETE/global-group policy, RTLD_GLOBAL/LOCAL flag expansion,
RTLD_NEXT, lazy binding, concrete APK/path search policy, and tombstone slot
reuse/compaction separate unless the targeted unload model proves one must be
represented at the same seam.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
