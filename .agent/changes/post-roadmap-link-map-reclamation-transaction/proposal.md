# Proposal — persistent link-map reclamation transaction

Build the first mutation layer on top of accepted reclamation planning.

Stable graph indexes remain permanent. Reclaimed objects become Retired
tombstones rather than being erased or compacted. Active identity lookup ignores
Retired slots, so a later load of the same identity allocates a new stable index.

A root-release transaction computes liveness after removing exactly one
persistent root, checks lifecycle eligibility, snapshots reclaimable mappings,
then unmaps them. Link-map/root/global/tombstone state is published only after
all unmaps succeed. Any unmap failure restores touched mappings from the
snapshots before returning.

This change does not run destructors. It only reclaims objects whose caller-owned
lifecycle state proves they were never constructed or have already completed
destruction.
