# Design — caller-supplied APK runtime bootstrap

Keep all accepted loader transactions authoritative.

The bootstrap is a persistent composition object, not a second loader. It owns:

1. bounded copies of caller-declared application SONAMEs;
2. one exact APK ABI virtual root;
3. exact requester identities for each declared application library;
4. the accepted A32ApkLibrarySource;
5. a tiny context-free provider for app-root dlopen/bootstrap only;
6. the accepted A32AndroidLibrarySearchProvider for requester-aware transitive
   application dependencies;
7. an ordered provider chain ending in one caller-owned platform provider; and
8. one A32LibDlOpenTransaction borrowing the caller's memory, persistent link
   map, handle table, and lifecycle state.

Context-free app-root provider lookup is whitelist-only. It resolves a declared
bare SONAME as `<apk>!/<abi-dir>/<soname>` and deliberately returns NotFound
for non-empty requester identity so transitive resolution is forced through the
existing requester-scoped search provider.

The requester search has one exact root record for every declared app identity.
This preserves feature-048 exact-requester semantics while allowing root -> child
-> sibling dependency chains inside one APK.

The initial bootstrap call requires its root SONAME to be in the declared set and
delegates directly to A32LibDlOpenTransaction::open. No relocation, RELRO,
constructor, handle, cleanup, or load-policy logic is reimplemented.

The composition remains alive after bootstrap and exposes the same open
transaction/provider chain for A32LibDlService dynamic dlopen.

Caller responsibility remains explicit: APK path, ABI directory, complete
finite app SONAME set, platform compatibility provider/policy, guest stack and
all existing resource ceilings. Automatic package/manifest/ABI discovery is a
later layer.
