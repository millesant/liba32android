# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-link-map-reclamation-planning` is DONE. Its validated result
revision is `8093ad7cc7ed83f97202eb87ff1da375406e6a9e`.

`post-roadmap-link-map-reclamation-transaction` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The mutation layer now preserves permanent stable graph slots, retires reclaimed
objects as tombstones, releases exact persistent roots, sweeps objects after the
last non-root owner disappears, lifecycle-gates physical reclamation, recomputes
global visibility, snapshots live mapping bytes/permissions, and publishes
link-map mutation only after successful unmapping.

After terminal success, the next ownership step should connect this foundation
to dynamic libdl acquisition/unload orchestration rather than teaching the
low-level transaction about dlopen policy. A higher layer can then make a
missing-object dlopen append a persistent root, run relocation/constructors,
create the synthetic handle ownership anchor, and on final close run teardown,
release that dynamic root, and invoke reclamation.

Keep RTLD_NODELETE/global-group policy explicit at that higher ownership seam;
do not silently fold it into generic graph reachability.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
