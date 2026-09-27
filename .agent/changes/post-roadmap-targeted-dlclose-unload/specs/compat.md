# Compatibility spec delta — targeted final-close dlclose unload

A32LibDlService may optionally delegate dlclose to a bounded
A32LibDlUnloadTransaction. This path has precedence over the accepted exact
resident-object close transaction; both older close paths remain available when
the unload transaction is absent.

Non-final references decrement only.

On the final reference, the transaction preserves every other live handle as an
ownership anchor, requires the current graph to contain no already-unowned
Active object, computes the exact post-root-release unreachable set, finalizes
only that set in requester-before-dependency order, and performs physical root
release/reclamation only after all selected lifecycle state is Complete.

Exact-object teardown remains FINI_ARRAY reverse order, exact registered
finalization completion, then DT_FINI. Complete destructor state is an
idempotent success for retry; Failed state is never replayed.

Lifecycle or reclamation failure preserves the final synthetic handle and exact
persistent root. Physical-unload delegation requires MappedGuestMemory.

Automatic DSO-handle discovery, RTLD_NODELETE/global-group policy, and
concurrent mutation remain separate.
