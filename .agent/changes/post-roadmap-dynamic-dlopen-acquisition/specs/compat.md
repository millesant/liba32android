# Compatibility spec delta — dynamic missing-object dlopen acquisition

Allow A32LibDlService to optionally delegate named dlopen requests to a bounded
open transaction.

The transaction considers only Active persistent-link-map slots. Active resident
objects may acquire/increment a synthetic handle only when constructor state is
Pending or Complete and destructor state remains Pending. Retired objects,
failed constructors, and completed/failed destructor state are not reopenable.

A missing name resolves through the caller-owned dependency-provider seam,
appends/reuses one Local persistent root, eagerly applies the accepted combined
relocation subset to objects newly appended by that operation using the current
persistent global scope, seals their GNU RELRO, and runs persistent constructors
from the root. Synchronous constructor execution uses trapped live guest r13.

Before constructor execution, failures remove a root record added by the attempt
and reclaim newly unreachable Pending/Pending mappings while treating every
currently live synthetic-handle object as an additional ownership anchor.

Constructor-stage failure remains resident with latched lifecycle state. A guest
handle is published only after successful initialization. Repeated resident
opens refcount the same object handle.

Recursive final-close unload lifecycle, RTLD_NODELETE, RTLD_GLOBAL/LOCAL
expansion, lazy binding, and pathname/search policy remain separate.
