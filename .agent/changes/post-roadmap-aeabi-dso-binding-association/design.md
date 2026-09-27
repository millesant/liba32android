# Design — lifecycle-scoped DSO binding association

The runtime must not guess an object's opaque C++ DSO handle from load bias,
ELF symbols, SONAME, or mapping addresses. Instead it learns the relationship
from the ABI event that already carries the DSO word: __aeabi_atexit.

Elf32InitExecutionOptions gains an optional caller-owned lifecycle execution
context. For every constructor/destructor guest call, the executor scopes that
context to the call's stable object index and restores the previous value on all
returns. Nested lifecycle execution therefore composes by saving/restoring the
outer object index.

A32AeabiAtexitService may additionally borrow finite caller-owned learned
binding storage and that same context. Legacy one-argument construction remains
unchanged.

When __aeabi_atexit succeeds under a known current object and r2 is non-zero,
the service learns object_index <-> dso_handle. Existing identical bindings are
reused. A conflicting mapping in either direction or exhausted binding storage
returns guest -1 and leaves both registration and learned state unchanged.

Registrations made without a current lifecycle object, or with dso_handle zero,
remain ordinary accepted records but do not invent an association.

A32LibDlCloseTransaction resolves each object's DSO from both caller-supplied
explicit bindings and the learned registration state. One source may satisfy the
lookup alone. If either source is internally ambiguous or both sources disagree,
the close is rejected as InvalidBinding.

This gives dynamic constructor registrations an evidence-backed path into later
targeted dlclose without requiring a static binding table.
