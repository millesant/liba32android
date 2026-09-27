# Proposal — dynamic missing-object dlopen acquisition

Add an optional compatibility transaction above the accepted provider,
persistent-link-map, relocation, RELRO, lifecycle, handle, and reclamation
primitives.

Named dlopen first resolves active resident state. Missing names are acquired
through the caller-owned dependency provider, appended as Local persistent
roots, relocated eagerly using the current persistent global scope, RELRO-sealed,
and initialized through persistent constructors. The guest-visible handle is
published only after initialization succeeds.

Failures before constructor execution may safely remove the just-added ownership
root and reclaim newly unreachable Pending/Pending mappings while preserving
objects kept live by existing handles. Constructor-stage failure remains
resident with latched lifecycle failure because arbitrary guest side effects are
not rollback-safe.

Recursive final-close unload is deliberately separate because shared
dependencies require teardown of only the objects that become unreachable after
ownership release.
