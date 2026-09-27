# ELF32 spec delta — persistent link-map reclamation transaction

Add permanent Active/Retired state for persistent link-map object slots. Retired
slots preserve their stable graph indexes, are excluded from active identity
reuse/roots/global scope/reachability, and are not recycled. Loading an identity
whose prior slot is Retired allocates a new accumulated slot.

Add a bounded root-release reclamation transaction. It computes liveness after
removing one exact persistent root while honoring caller live anchors. Only
active unreachable objects in lifecycle state Pending/Pending or
Complete/Complete may be physically reclaimed.

Before mutation the transaction snapshots every reclaimable load mapping under
caller-selected segment and byte ceilings, including current bytes and per-page
permissions. Unmap failure restores every touched mapping; rollback failure is
reported distinctly. Link-map root/global/state mutation is published only
after all unmaps succeed.

The transaction does not execute destructors, apply RTLD_NODELETE, reuse or
compact Retired slots, coordinate concurrent graph mutation, or dynamically
acquire missing objects.
