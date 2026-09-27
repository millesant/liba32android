# Design — targeted final-close unload

The unload transaction composes the accepted ownership planner, exact-object
Android teardown transaction, and physical reclamation transaction.

For a non-final synthetic reference, only the refcount changes.

For the final reference, every other live handle object becomes an additional
ownership anchor. The transaction first runs ordinary reclamation planning
against the current link map and requires zero reclaimable Active objects. This
prevents a close operation from opportunistically collecting unrelated
pre-existing orphan state.

It then runs read-only root-release planning as if the closing object's exact
persistent root were removed. The resulting reclaimable list is therefore the
exact newly unreachable teardown set. The planner preserves deterministic
requester-before-dependency ordering.

Each selected object is finalized through the accepted exact-object teardown
primitive. That primitive is now idempotent for already-Complete destructor
state, allowing a retry after a later selected object or the physical
reclamation stage failed. Guest-side failure still latches Failed and prevents
replay.

The final handle and root remain live throughout lifecycle teardown. After every
selected object is Complete, physical root release receives the same other-live-
handle anchor set. Its existing snapshot/unmap transaction removes the root and
retires/unmaps only the now-unreachable objects. The synthetic handle is cleared
only after physical release succeeds.

A configured libdl service may delegate dlclose to this transaction. Because
physical reclamation requires map/protect/unmap operations, delegation accepts
only a MappedGuestMemory backend; other GuestMemory implementations fail
gracefully rather than being downcast unsafely.

Object-to-DSO bindings remain caller-supplied. Automatic discovery of a dynamic
object's opaque __dso_handle is not invented here.
