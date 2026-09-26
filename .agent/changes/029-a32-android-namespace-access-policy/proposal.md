# Proposal — direct Android namespace SONAME accessibility

## Intent

Use the requester-aware feature-028 platform-provider seam to model the smallest
Android namespace behavior required by the observed target dependency mix.

Do not implement filesystem search or full bionic linker namespaces. Add only a
caller-configured requester -> namespace binding and direct linked-namespace
SONAME gate.

## AOSP-shaped rule

A direct namespace link exposes either:

- every shared library; or
- a selected set of shared-library SONAMEs.

The two modes are mutually exclusive. This is the same bounded accessibility
shape used by bionic linked namespaces.

## Project model

The policy is configured with:

- finite exact requester-identity -> namespace bindings;
- finite direct namespace links;
- one platform namespace name corresponding to the feature-028 provider.

Same-namespace access is allowed. Cross-namespace access requires one exact
direct link and its SONAME rule. Missing accessibility is `NotFound`;
ambiguous/malformed matching configuration is `Failed`.

## Real integration

Replace the unconditional recording policy in the ARM32 liblog integration with
a real namespace policy:

`android-log-consumer -> app --[liblog.so]--> platform`.

Because dependency loading forwards the actual root identity, any requester
drift makes the binding miss and the real dependency graph fail before
relocation/execution.

## Non-goals

No path-to-namespace inference, search/permitted paths, config-file parser,
transitive link traversal, filesystem/APK provider, RUNPATH/RPATH,
LD_LIBRARY_PATH, preload/RTLD flags, or new platform-library shim.
