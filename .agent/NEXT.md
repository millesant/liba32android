# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-link-map-reclamation-transaction` is DONE. Its validated result
revision is `33037680cdf3dd25a7b60dc12b051fef5cfebf98`.

Next, implement bounded dynamic missing-object libdl acquisition above the
accepted persistent loader/lifecycle/reclamation primitives.

The first slice should resolve one requested root through the caller-owned
provider seam, append/reuse it as a persistent Local root, eagerly apply the
accepted relocation subset to newly loaded objects, seal their GNU RELRO,
run persistent constructors without replaying already-Complete shared
dependencies, and publish/increment a synthetic handle only after successful
initialization.

Preflight handle capacity before mutating a genuinely new load. Failures before
guest constructor execution should release the just-added root and physically
reclaim newly unreachable Pending/Pending objects. Constructor execution failure
must remain resident with latched lifecycle failure rather than pretending guest
side effects can be rolled back.

Keep recursive final-close unload lifecycle separate: it must finalize only the
objects that become unreachable after releasing the dynamic owner, not blindly
destroy an entire root closure containing shared dependencies.

Keep RTLD_NODELETE/global-group policy explicit and separate.

Other ready work remains broader pthread/TLS, concrete APK/ZIP acquisition, and
higher-level public ELF/platform embedding.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
