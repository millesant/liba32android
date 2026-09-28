# Design — ARM32 libdl load policy

The compatibility ABI must use bionic LP32 values rather than generic/LP64
dlfcn constants. In particular RTLD_NOW is zero and RTLD_GLOBAL occupies bit 1
on ARM32.

Dlopen mode parsing therefore validates only the supported modifier bitmask.
RTLD_LAZY is accepted for ABI compatibility but does not enable lazy
relocations; the existing eager relocation transaction remains authoritative.

Persistent roots gain a monotonic nodelete bit. Root visibility and retention
are independent: Global controls symbol visibility; nodelete controls whether
final handle release may perform lifecycle/root reclamation.

A small persistent-root policy mutation primitive adds an exact root for an
already-Active object when needed, promotes Local to Global without demotion,
sets nodelete monotonically, and updates deterministic global scope.

Named successful dlopen always leaves such a root record. RTLD_NOLOAD performs
only the resident lookup/policy/handle path and never calls the provider.

Targeted unload checks nodelete before lifecycle planning. On the final
synthetic reference, a nodelete root keeps lifecycle/mappings/root unchanged and
only clears the handle. Later opens inherit the persistent root policy.

RTLD_DEFAULT/RTLD_NEXT are corrected to the bionic LP32 sentinel values and
synthetic-handle generation rejects both sentinels.
