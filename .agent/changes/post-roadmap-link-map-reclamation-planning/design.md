# Design — persistent link-map reclamation planning

The existing `Elf32LinkMap` keeps stable accumulated object indexes, so safe
unload cannot begin by erasing graph entries. Liveness is modeled separately.

The planner accepts the persistent link map plus a borrowed finite set of
additional live object anchors. Every persistent `link_map.roots` entry is an
ownership anchor. Additional anchors represent external owners such as active
libdl handles. Duplicate anchors are harmless.

Reachability walks dependency edges transitively and visits each object at most
once. Cycles and shared dependencies therefore require no recursive reference
counter and cannot underflow or double-release. `global_scope_objects` is
visibility metadata only and does not retain an otherwise unreachable object.

A successful plan contains reachable object indexes in stable ascending index
order and unreachable objects in deterministic requester-before-dependency
teardown order. The latter is produced from the unreachable subgraph only and
is suitable as input to a later lifecycle/reclamation transaction.

Before success the planner validates its bounded object ceiling, persistent root
records, dependency-edge targets/names, and additional live anchors. It is
strictly read-only: no lifecycle state, handle state, graph/root/global vectors,
mapping, or protection may change.

Actual destructors, root removal, global-scope pruning, tombstoning/index reuse,
and guest unmapping remain follow-up work.
