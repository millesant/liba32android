# Compatibility spec delta — feature 029

Add `A32AndroidNamespaceAccessPolicy` as a concrete implementation of the
feature-028 platform access-policy seam.

The caller supplies finite borrowed exact requester-identity -> namespace
bindings, finite borrowed direct namespace links, and one non-empty platform
namespace name.

An exact requester binding is required. No binding is `NotFound`; duplicate
matching bindings or an empty matching namespace are `Failed`. A requester
already in the platform namespace is `Allow`.

Otherwise exactly one direct requester-namespace -> platform-namespace link is
required. No link is `NotFound`; duplicate matching links are `Failed`. The
matching link must select exactly one mode: allow all with an empty explicit
list, or a non-empty list of non-empty exact SONAMEs. Exact-list miss is
`NotFound`; malformed mode/list configuration is `Failed`.

Unrelated malformed records do not poison exact lookup. The policy allocates
nothing and owns no binding/link/string storage.

Do not infer namespace membership from paths, traverse links transitively,
perform filesystem/APK search, parse Android linker configuration, implement
RUNPATH/RPATH/LD_LIBRARY_PATH/preload/RTLD behavior, or add another platform
shim.
