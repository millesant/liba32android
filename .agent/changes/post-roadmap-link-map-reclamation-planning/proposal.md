# Proposal — persistent link-map reclamation planning

Add a read-only bounded planning seam that separates object liveness from
physical link-map storage.

Persistent link-map roots remain ownership anchors. Callers may additionally
supply live object anchors such as resident libdl handles. The planner computes
the transitive dependency closure of those anchors and identifies every object
outside that closure as reclaimable.

This change intentionally does not execute destructors, remove root/global
records, erase graph slots, recycle stable object indexes, or unmap guest
memory. It establishes the ownership/reachability facts those later mutations
must consume.
