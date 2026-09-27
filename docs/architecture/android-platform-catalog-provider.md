# Android finite platform compatibility catalog

Status: feature 031 accepted; finite catalog reused by features 032+ including feature 046 libdl

## Need

Feature 028 intentionally hardcodes one platform compatibility slot:
`liblog.so`. Feature 030 establishes a concrete second platform target,
`libc.so`, for a bounded memory/string service slice.

Feature 031 adds the finite multi-entry provider needed to expose more than one
compatibility library without adding pathname/search behavior.

## Provider

`A32AndroidPlatformCatalogProvider` implements the existing requester-aware
`Elf32DependencyProvider` contract and borrows:

- a finite span of `Elf32DependencyCatalogEntry` records;
- every entry's requested-name/identity/image backing storage;
- one `A32AndroidPlatformAccessPolicy`.

For a request, the provider first scans only for an exact catalog name. No match
returns `NotFound` without invoking policy. Duplicate exact names are
`Failed` before policy.

Exactly one name match is passed byte-for-byte to policy. `Allow` delegates to
the existing `Elf32DependencyCatalogProvider`, preserving its non-empty
identity/image and maximum-image-byte validation. Policy `NotFound` and
`Failed` propagate without source data.

Context-free `resolve` uses an empty requester identity.

## Composition

The existing feature-028 `A32AndroidPlatformProvider` remains as a
source-compatible one-slot `liblog.so` convenience provider.

The new finite provider composes directly with feature 029's namespace policy.
Focused coverage established a two-entry `liblog.so` + `libc.so` catalog and
an `app -> platform` explicit SONAME link. Later partial-libc integrations
reuse the same seam. Feature 046 additionally uses the generic finite catalog
for generated `libdl.so` while an application catalog supplies a separate
resident target DSO, proving platform libdl selection remains namespace-gated
without embedding pathname/search policy.

## Ownership

The generic provider owns no entries or image bytes. Its external entry span,
nested storage, and policy object must outlive it. It is deliberately
non-copyable/non-movable so lifetime assumptions remain explicit.

## Non-goals

No automatic process/platform catalog construction, filesystem/APK search,
path normalization, namespace inference, guest libc.so binary, allocator/thread
behavior, or additional Android library implementation is introduced.
