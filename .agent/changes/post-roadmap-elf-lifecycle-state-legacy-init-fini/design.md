# Design — persistent ELF lifecycle state and legacy functions

## Metadata

DT_INIT and DT_FINI are unique singleton dynamic tags. Their raw values are
preserved during collection and rebased once by load bias during validation.
Only 32-bit overflow is rejected at metadata time; executable fetch validity
belongs to the lifecycle CPU seam.

## Persistent state

Each graph object has constructor and destructor status:
Pending, Complete, or Failed.

State is caller-owned, follows stable graph object indexes, may grow to match a
larger persistent graph, and may not outgrow the graph. Destructors may not be
non-Pending unless constructors are Complete.

## Constructors

A root traversal is dependency-first with transient cycle suppression and a
caller unique-object ceiling. Complete objects are skipped. New objects execute
DT_INIT followed by declaration-order INIT_ARRAY. One total raw array-entry
ceiling applies across the invocation.

If guest execution fails, the affected object's constructor status becomes
Failed. Possible partial guest side effects make replay unsafe, so subsequent
visits fail explicitly.

## Destructors

Reachable objects must have Complete constructors. Requesters execute before
dependencies. Each object runs reverse FINI_ARRAY followed by DT_FINI. Complete
destructors are skipped; execution failure latches Failed.

This does not own shared-object reference counts or decide when a root is safe
to destroy.
