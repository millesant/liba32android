# Proposal — finite requester-aware Android platform catalog

## Intent

Remove feature 028's deliberate one-library ceiling without changing generic
ELF provider APIs or inventing path/search behavior.

Feature 030 creates a concrete reason for a second platform compatibility
library: a future guest libc.so shim for seven bounded memory/string services.

## Provider

Add A32AndroidPlatformCatalogProvider over a caller-owned finite span of
Elf32DependencyCatalogEntry records plus the existing
A32AndroidPlatformAccessPolicy.

Unknown names bypass policy. Duplicate exact names fail. A unique exact name is
policy-gated, then existing catalog validation/copy semantics are reused.

## Compatibility

Keep A32AndroidPlatformProvider unchanged for existing liblog.so callers. The
new class is additive.

## Non-goals

No default catalog, no embedded images, no guest libc.so, no path/namespace
construction, and no filesystem/APK search.
