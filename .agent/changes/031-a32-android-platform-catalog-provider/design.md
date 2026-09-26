# Design — finite requester-aware Android platform catalog

## API

A32AndroidPlatformCatalogProvider borrows:

- std::span<const Elf32DependencyCatalogEntry> entries;
- A32AndroidPlatformAccessPolicy& policy.

It internally constructs an Elf32DependencyCatalogProvider over the same
external span. Copy/move are deleted to make non-owning lifetime assumptions
explicit.

## Resolution

resolve_for(requester,name,max):

1. empty requested name -> NotFound;
2. scan the finite catalog for exact requested-name matches;
3. no match -> NotFound without policy;
4. duplicate exact matches -> Failed without policy;
5. unique match -> call policy with exact requester/name views;
6. Allow -> delegate to Elf32DependencyCatalogProvider::resolve;
7. NotFound/Failed -> propagate empty-source result.

resolve(name,max) uses an empty requester.

Delegation intentionally keeps catalog validation of identity/image/byte ceiling
centralized instead of duplicating it.

## Composition

Focused coverage builds a two-entry synthetic platform catalog containing
liblog.so and libc.so. Feature-029 namespace policy exposes those exact SONAMEs
from app to platform and allows libc.so for a bound requester.

## Compatibility

The feature-028 one-slot liblog provider remains untouched and valid.

## Non-goals

No provider-chain installation, search paths, APK reading, namespace inference,
guest libc binary, allocator semantics, or platform image ownership.
