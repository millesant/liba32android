# Design — ARM32 libdl load policy

The compatibility ABI must use bionic LP32 values rather than generic/LP64
dlfcn constants. In particular RTLD_NOW is zero and RTLD_GLOBAL occupies bit 1
on ARM32.

Dlopen mode parsing therefore validates only the supported modifier bitmask.
RTLD_LAZY is accepted for ABI compatibility but does not enable lazy
relocations; the existing eager relocation transaction remains authoritative.

Persistent roots gain a monotonic nodelete bit. Global controls symbol
visibility and, matching bionic soinfo::can_unload(), a linked Global root is
also retained on final close. NODELETE independently pins a Local or Global
root without adding symbol visibility.

A small persistent-root policy mutation primitive adds an exact root for an
already-Active object when needed, promotes Local to Global without demotion,
sets nodelete monotonically, and updates deterministic global scope.

Named successful dlopen always leaves such a root record. RTLD_NOLOAD performs
only the resident lookup/policy/handle path and never calls the provider.

Both exact-object close and targeted unload check the persistent root policy
before lifecycle planning. On the final synthetic reference, a Global or
NODELETE root keeps lifecycle/mappings/root unchanged and only clears the
handle. Later opens inherit the monotonic persistent root policy.

RTLD_DEFAULT/RTLD_NEXT are corrected to the bionic LP32 sentinel values and
synthetic-handle generation rejects both sentinels.
