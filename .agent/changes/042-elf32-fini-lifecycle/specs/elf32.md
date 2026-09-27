# ELF32 spec delta — feature 042

Add root-scoped FINI_ARRAY destructor planning over the accepted dependency
graph. The planner records dependency-first postorder once, emits objects in
exact reverse order, decodes each FINI_ARRAY within caller-wide object/entry
ceilings, and emits non-sentinel entries in reverse declaration order.

Execution reuses the accepted bounded ARM/Thumb lifecycle call seam. It owns no
guest mappings, stack allocation, rollback, persistent constructor/destructor
state, unload policy, or Android-specific namespace/runtime behavior.

Legacy DT_INIT/DT_FINI, PREINIT_ARRAY, called-state/recursion persistence,
dlopen/dlsym/unload, process argv/envp constructor ABI, and device execution
remain deferred.
