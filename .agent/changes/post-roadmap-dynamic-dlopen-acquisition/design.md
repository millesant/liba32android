# Design — dynamic dlopen acquisition

`A32LibDlOpenTransaction` borrows one `MappedGuestMemory`, persistent link
map, dependency provider, synthetic handle table, and persistent lifecycle
state. Its options bundle the already-accepted loader, relocation, RELRO,
constructor, reclamation, and handle-base ceilings.

Named lookup considers Active slots only. An Active resident object may be
opened only while destructor state is Pending and constructor state is Pending
or Complete. Failed constructors or any completed/failed destructor state reject
new ownership.

For a missing name, provider resolution occurs at the root boundary. A new
identity requires a free synthetic handle slot before append. The root is
appended as Local. The transaction remembers the prior object count and whether
that exact root record existed before the call.

Only newly appended objects are relocated and RELRO-sealed. Relocation options
consume the post-append persistent global scope. Persistent constructors then run
from the root and naturally skip already-Complete shared dependencies.

When called synchronously from guest dlopen, constructor execution copies the
configured lifecycle options and replaces stack_top with trapped guest r13 so
nested guest calls cannot overwrite the active caller frame.

Before constructor execution, failure cleanup removes a root record added by
this attempt and invokes physical reclamation with every currently live handle
object as an additional ownership anchor. Cleanup failure is surfaced
distinctly.

Constructor-stage failure is not cleaned up: lifecycle may already be Failed or
guest side effects may have occurred. The root therefore remains resident and
future open attempts reject the failed lifecycle state.

Handle allocation happens after successful initialization. Capacity is
preflighted for genuinely new roots. If re-entrant constructor activity consumes
that capacity before publication, the initialized root remains resident and the
publication failure is explicit rather than destroying initialized state.

This change does not implement recursive final-close lifecycle, RTLD_NODELETE,
RTLD_GLOBAL/LOCAL flags, lazy relocation, or filesystem/pathname policy.
