# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-dynamic-dlopen-acquisition` is DONE. Its validated result
revision is `40479ffebe61c22c90d7d523292fc9e787be635d`.

Next, implement targeted final-close unload lifecycle over only objects that
become unreachable after releasing one dynamic ownership root.

Compute post-release reachability before ownership mutation. Remaining
persistent roots plus every other live synthetic-handle object are ownership
anchors. Shared dependencies reachable from those anchors must stay initialized
and mapped.

For the newly unreachable set, run teardown in deterministic
requester-before-dependency order. Each selected object must preserve the
accepted Android ordering: FINI_ARRAY reverse order, exact registered
`__cxa_finalize` completion for its bound DSO handle, then DT_FINI. Teardown
failure must preserve the final handle/root ownership and latch failure rather
than publishing partial unload.

Only after every selected object completes teardown should the final handle be
released, the exact dynamic root be released, and the accepted physical
reclamation transaction retire/unmap the now-unreachable slots.

Do not use whole-root persistent destructor traversal because it would destroy
shared dependencies that remain owned by another root/handle.

Keep RTLD_NODELETE/global-group policy, RTLD_GLOBAL/LOCAL expansion, RTLD_NEXT,
lazy binding, pathname/APK policy, and tombstone compaction separate.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
