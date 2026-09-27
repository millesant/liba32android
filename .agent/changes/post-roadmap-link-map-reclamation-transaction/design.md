# Design — persistent link-map reclamation transaction

## Stable slot state

`Elf32LinkMap` owns one state per accumulated graph slot: Active or Retired.
Legacy empty state is interpreted as all Active and is materialized before
successful append/reclamation mutation. Retired slots never return to Active and
are never recycled or compacted by this change.

Append preflight requires any explicit state vector to match graph size. Active
object identities remain unique. Retired identities are ignored for identity
reuse, so reloading the same bytes/name allocates a new slot at the end. Active
dependency edges, roots, and global-scope records may never target Retired
objects.

## Root release and liveness

The transaction releases exactly one active persistent root record. Before
mutation it computes reachability as though that record were absent, using all
other persistent roots plus caller-supplied live object anchors. Retired slots
are excluded from traversal.

Active objects outside that closure are reclaim candidates in deterministic
reverse-postorder teardown order.

## Lifecycle gate

The caller supplies persistent lifecycle state covering every graph slot. A
reclaim candidate is eligible only when it is either:

- never constructed: constructors Pending and destructors Pending; or
- fully torn down: constructors Complete and destructors Complete.

Failed constructors/destructors and Complete/Pending partial teardown are not
physically reclaimed by this transaction.

## Mapping transaction

Before the first unmap, every reclaimable PT_LOAD mapping is validated, bounded,
and snapshotted. Snapshots include the full mapped bytes and the current
permission of every page so prior RELRO/protection changes are preserved.

Unmaps execute in reclamation teardown order. If any unmap fails, every touched
mapping including the failing range is restored from its snapshot. Restoration
can recover either an already-unmapped range or a still-mapped range whose
contents were discarded before the backend reported failure. A restoration
failure is surfaced distinctly.

Only after every unmap succeeds are the precomputed root/global/state vectors
moved into the caller-owned link map. Thus a normal failure publishes no partial
link-map mutation.

## Deliberate limits

This transaction does not execute lifecycle callbacks, implement RTLD_NODELETE,
reuse Retired slots, compact graph indexes, coordinate concurrent mutation, or
perform dynamic missing-object dlopen.
