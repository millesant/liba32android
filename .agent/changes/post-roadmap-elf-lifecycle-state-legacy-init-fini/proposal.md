# Proposal — persistent ELF lifecycle state and legacy DT_INIT/DT_FINI

The accepted array lifecycle seam can execute INIT_ARRAY/FINI_ARRAY but does
not remember whether an object has already run constructors/destructors, and
linker metadata still ignores legacy DT_INIT/DT_FINI.

Add one caller-owned persistent state vector keyed by stable graph object index,
recognize/rebase the legacy function tags, and compose them with the existing
bounded ARM/Thumb lifecycle executor.

This establishes the once-only lifecycle substrate needed before registered
C++ destructors and real dynamic dlopen/unload ownership.
