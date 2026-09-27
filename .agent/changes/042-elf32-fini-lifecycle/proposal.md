# Proposal — bounded FINI_ARRAY destructor lifecycle

Complete the array-based ELF32 lifecycle seam by adding root-scoped
FINI_ARRAY planning and bounded execution.

The planner operates on the same reachable dependency graph semantics as
constructor planning, but emits the exact reverse object order and reverse each
object's FINI_ARRAY entries. Sentinel values remain filtered only at planning
policy while still consuming the caller's raw-entry budget.

Execution reuses the accepted feature-019 ARM/Thumb bounded-call seam rather
than inventing a second ABI or executor.

Non-goals are legacy DT_INIT/DT_FINI, PREINIT_ARRAY, persistent called-state,
runtime recursion guards, dlopen/dlsym/unload/refcount orchestration, process
argv/envp constructor ABI, or Android-device execution.
