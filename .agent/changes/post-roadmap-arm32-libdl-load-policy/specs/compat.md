# Compatibility/ELF spec delta — ARM32 libdl load policy

The ARM32 compatibility ABI uses bionic's LP32 dlfcn values rather than the
generic/LP64 layout:

- RTLD_LOCAL = 0
- RTLD_NOW = 0
- RTLD_LAZY = 1
- RTLD_GLOBAL = 2
- RTLD_NOLOAD = 4
- RTLD_NODELETE = 0x1000
- RTLD_DEFAULT = 0xffffffff
- RTLD_NEXT = 0xfffffffe

RTLD_LAZY is accepted as an ABI flag only. Android/bionic does not implement
lazy binding and the compatibility runtime continues to perform the accepted
eager main+PLT relocation transaction before dlopen returns.

Every successful named dlopen owns one exact Active persistent root, even when
the target had previously existed only as another root's dependency. Local is
the default absence of Global. Root visibility is monotonic: Global promotion
is deterministic and a later Local open never demotes it.

NODELETE is a separate monotonic root property. Once set by any successful
open, later opens without NODELETE do not clear it. Final synthetic-handle
release for a NODELETE root clears only the handle; exact-object teardown,
registered finalization, root release, tombstoning, unmapping, and learned-DSO
binding retirement are skipped.

NOLOAD is a resident query. It may acquire, Global-promote, and/or NODELETE-pin
an already-Active matching object, including an Active dependency that did not
previously have its own root. A missing NOLOAD target returns null and does not
invoke the dependency provider or create mappings.

Policy is published only after a synthetic handle reference has been acquired.
If root-policy mutation fails, that unexposed handle reference is rolled back.
For newly initialized dynamic objects whose later publication cannot complete,
the existing constructor-side-effect retention rules still apply.

RTLD_DEFAULT and RTLD_NEXT use the LP32 sentinel values above; generated
synthetic handles must never collide with either. RTLD_DEFAULT continues the
accepted first-root/global-root lookup. RTLD_NEXT lookup semantics remain
unsupported and return the existing guest-visible error.

Generic ELF root-release/reclamation does not interpret NODELETE by itself.
NODELETE is a libdl ownership policy enforced before invoking the generic
lifecycle/reclamation transaction.
