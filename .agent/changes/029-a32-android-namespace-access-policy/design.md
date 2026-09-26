# Design — direct Android namespace SONAME accessibility

## Types

Add:

- `A32AndroidNamespaceBinding { requester_identity, namespace_name }`;
- `A32AndroidNamespaceLink { from_namespace, to_namespace,
  allow_all_shared_libs, shared_libs }`;
- `A32AndroidNamespaceAccessPolicy final : A32AndroidPlatformAccessPolicy`.

Every view/span is borrowed from the caller for the policy lifetime. The policy
allocates nothing.

## Requester binding

For one decision, scan the finite binding span for exact requester identity.
Unrelated records are ignored.

- zero matches -> `NotFound`;
- exactly one non-empty namespace -> continue;
- duplicate exact matches or an empty matching namespace -> `Failed`.

An empty requester is treated as unbound and returns `NotFound`.
An empty configured platform namespace is invalid and returns `Failed`.

## Direct namespace access

If requester namespace equals platform namespace, return `Allow`.

Otherwise scan for an exact direct link whose source is requester namespace and
destination is platform namespace.

- zero matches -> `NotFound`;
- duplicate matches -> `Failed`.

The matching link then validates its accessibility mode:

- `allow_all_shared_libs=true` requires an empty explicit list and returns
  `Allow`;
- explicit mode requires a non-empty list and every entry must be non-empty;
- exact requested SONAME in that list -> `Allow`;
- otherwise -> `NotFound`;
- combining allow-all and explicit names, or supplying neither, -> `Failed`.

No transitive link traversal occurs.

## Integration

The real liblog integration configures one binding for
`android-log-consumer`, one explicit link from `app` to `platform`, and
one allowed SONAME `liblog.so`. Existing application-first/platform-fallback
provider ordering stays unchanged.

This connects requester-aware dependency loading to a concrete namespace gate
without modifying the ELF resolver/provider interfaces.

## Verification

Focused deterministic tests cover exact byte identity, same namespace,
allow-all, explicit allow/deny, malformed/ambiguous matching configuration, and
unrelated malformed records. The real ARM32 shim integration remains the
cross-layer proof.

Exact-head CI is intentionally deferred until feature 028's current exact-head
acceptance completes; preparing this commit must not move `bleeding` and cancel
that validation run.
