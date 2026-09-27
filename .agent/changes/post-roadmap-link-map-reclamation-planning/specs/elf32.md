# ELF32 spec delta — persistent link-map reclamation planning

Add a bounded read-only planner over the persistent link map.

Persistent link-map roots are ownership anchors. A caller may supply additional
live object anchors representing external owners such as active libdl handles.
The planner follows dependency edges transitively, suppresses cycles/repeated
edges/shared dependencies, and reports stable-index reachable objects plus
deterministic requester-before-dependency unreachable candidates.

Global-scope membership is visibility rather than ownership and therefore does
not retain an otherwise unreachable object.

Malformed roots/edges/live anchors and caller object ceilings fail explicitly.
Planning does not run lifecycle code, remove root/global records, erase or reuse
stable object slots, change handles, or unmap/protect guest memory.
