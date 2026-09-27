# Design — bounded FINI_ARRAY destructor lifecycle

## Object order

Traverse reachable objects dependency-first with transient unseen/visiting/
complete state and the same stored edge order used by INIT_ARRAY planning.
Record one postorder object index after dependencies complete. Cycles and shared
dependencies therefore contribute each object at most once.

Destructor planning walks that completed postorder backwards. This produces the
exact reverse of constructor object order even when two requesters share one
dependency, which a naive requester-first recursive walk would not guarantee.

## Array order and limits

For each object in reverse postorder, decode its validated FINI_ARRAY through
the feature-017 decoder using the remaining caller-wide raw-entry budget.
Iterate decoded entries backwards. Null and all-ones values consume budget but
are skipped as executable calls. Retained calls keep object, original array
index, and raw function value including the Thumb discriminator.

Failures return no successful partial call vector and retain object/decode
provenance.

## Execution

FINI calls have the same per-call CPU ABI as INIT calls, so
execute_elf32_fini_calls delegates to the feature-019 bounded executor. Stack,
return-stop, instruction-budget, fault, and guest-side-effect semantics remain
identical.

## Non-goals

No DT_INIT/DT_FINI, PREINIT_ARRAY, persistent called/recursion state,
dlopen/dlsym/unload/refcount policy, process argv/envp ABI, or device execution.
