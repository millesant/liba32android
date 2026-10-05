# Android namespace accessibility policy

Status: accepted current architecture; bounded namespace accessibility policy implemented

## Boundary

`A32AndroidNamespaceAccessPolicy` implements the feature-028
`A32AndroidPlatformAccessPolicy` seam. It decides only whether one requester
may access a named library from the configured platform namespace.

It does not load files, choose search paths, parse Android linker config, or
change generic ELF dependency-provider APIs.

## Configuration

The caller owns three finite inputs for the full policy lifetime:

- requester bindings: exact opaque requester identity -> namespace name;
- direct namespace links: source namespace -> destination namespace plus one
  SONAME-access mode;
- the platform namespace name used by the concrete platform provider.

All strings/spans are borrowed. The policy allocates and owns nothing.

## Decision algorithm

For one `decide(requester,name)` call:

1. an empty configured platform namespace is `Failed`;
2. empty requester/name or no exact requester binding is `NotFound`;
3. duplicate matching requester bindings or an empty matching namespace are
   `Failed`;
4. if requester namespace equals the platform namespace, return `Allow`;
5. otherwise find exactly one direct requester-namespace -> platform-namespace
   link;
6. no such link is `NotFound`; duplicate matching links are `Failed`;
7. `allow_all_shared_libs=true` requires an empty explicit list and allows the
   request;
8. explicit mode requires a non-empty list of non-empty names and allows only
   an exact requested-name match.

Malformed unrelated bindings/links do not poison an exact request.

This mirrors the bounded AOSP concept that linked namespaces expose either a
selected SONAME set or all shared libraries. The feature intentionally does not
perform transitive link traversal.

## Integration

The real ARM32 liblog fixture now configures:

`android-log-consumer -> app -> platform -> liblog.so`.

The application catalog remains first in the generic provider chain. Its
`NotFound` falls through to the feature-028 platform provider, which asks this
namespace policy. If the requester identity were not propagated unchanged, the
binding would miss and the dependency graph could not load. Successful graph
load therefore exercises the requester-aware namespace gate before the existing
JUMP_SLOT, real shim SVC, and log-service execution path.

## Non-goals

No filesystem/APK lookup, search/permitted paths, RUNPATH/RPATH,
LD_LIBRARY_PATH, public-library config parsing, namespace creation from process
state, preload/RTLD behavior, transitive namespace links, or additional Android
platform shims are introduced.
