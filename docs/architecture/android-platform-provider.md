# Android platform-library provider seam

Status: accepted current architecture; bounded platform-library provider seam implemented

## Boundary

`src/compat/a32_android_platform_provider.*` adds the first explicit
platform-library selection seam above the generic requester-aware ELF provider
contracts.

It is intentionally narrower than Android namespace emulation. Feature 028 has
one recognized platform compatibility library: the generated feature-027
`liblog.so` write shim.

## Request flow

For an exact `liblog.so` request:

1. preserve the requester's opaque identity and requested name exactly;
2. call the caller-owned `A32AndroidPlatformAccessPolicy`;
3. `Allow` delegates to the existing exact-name catalog provider;
4. `NotFound` lets an enclosing provider chain continue/fail normally;
5. `Failed` is a hard provider failure.

Unknown requested names return `NotFound` without invoking the policy. This
prevents a platform policy from accidentally aliasing arbitrary dependency names
onto the one implemented shim.

The context-free provider API remains supported and calls policy with an empty
requester identity.

## Ownership

The caller owns the access-policy object and the liblog image bytes for the
provider lifetime. The platform provider owns one catalog-entry object but only
borrows the image span.

Because the internal `Elf32DependencyCatalogProvider` stores a span into that
member entry, `A32AndroidPlatformProvider` is deliberately non-copyable and
non-movable.

## Composition

The platform provider is designed to sit after an application-local provider in
the existing `Elf32DependencyProviderChain`. Feature-028 regression coverage
proves an application catalog can return `NotFound`, after which the platform
provider receives the original requester identity unchanged.

The real ARM32 log-shim integration is also switched to this application-first
chain. It therefore proves the actual consumer's `DT_NEEDED liblog.so`
request reaches platform policy with requester identity
`android-log-consumer` before loading/relocation/execution continue through the
feature-027 shim.

## Non-goals

No Android namespace names, linked-namespace graph, permitted paths, public
library allowlists, filesystem/APK search, RUNPATH/RPATH, LD_LIBRARY_PATH,
preload/RTLD policy, or automatic platform-provider installation is introduced.
