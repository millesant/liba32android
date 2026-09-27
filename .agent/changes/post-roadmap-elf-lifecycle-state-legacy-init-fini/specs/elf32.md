# ELF32 spec delta — persistent legacy lifecycle

Add validated logical DT_INIT/DT_FINI entry points and caller-owned persistent
per-object lifecycle status.

Persistent constructor execution is dependency-first and once-only, with
DT_INIT before INIT_ARRAY. Persistent destructor execution is requester-first
and once-only, with reverse FINI_ARRAY before DT_FINI. Both reuse the bounded
ARM/Thumb call seam and caller ceilings.

Guest execution failure latches Failed state to prevent replay of partial side
effects. Registered __aeabi_atexit destructors, DT_PREINIT_ARRAY, argv/envp,
automatic dlopen transactions, reference-counted unload, and unmapping remain
separate.
