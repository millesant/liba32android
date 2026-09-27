# Compatibility spec delta — resident last-reference dlclose

Allow resident libdl dlclose to delegate to a bounded exact-object lifecycle
transaction.

Final-close ordering is reverse FINI_ARRAY, exact-DSO registered-finalization
verification, DT_FINI, lifecycle Complete, then synthetic handle release.
Failure after guest teardown begins latches Failed and preserves ownership.

Dependencies stay resident and mappings are not reclaimed.
