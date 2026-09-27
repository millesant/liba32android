# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-resident-dlclose-lifecycle-transaction` is DONE. Its validated
result revision is `efa2ce78e7d77ddf9c92ee29cdbe5a1f03fc6dd2`.

`post-roadmap-link-map-reclamation-planning` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The planner now separates stable link-map storage from liveness: persistent roots
plus borrowed live object anchors determine transitive reachability, while
global-scope visibility alone does not retain ownership. Unreachable objects are
returned in bounded deterministic reverse-postorder without mutating lifecycle
or guest memory.

After terminal success, build the first mutation transaction on top of that
plan: define explicit owner/root release, lifecycle eligibility, global-scope
pruning, stable-slot/tombstone policy, and transactional guest unmapping with
failure/rollback semantics. Do not erase stable graph indexes ad hoc.

Keep RTLD_NODELETE/global-group policy and dynamic missing-object dlopen separate
unless the reclamation transaction proves they must be represented at the same
seam.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
