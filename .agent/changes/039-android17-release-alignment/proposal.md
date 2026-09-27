# Proposal — align Android compatibility with Android 17 release

## Intent

Audit the prepared Android-facing compatibility lineage against the official
current AOSP release baseline instead of silently inheriting unqualified
`main`/`master` behavior.

## Baseline

Resolve `android-latest-release`, record its selected release, and use exact
Android 17.0.0 r1 source where available.

## Corrections

The audit found two real feature-033 access-order issues:

- memmem validated ranges that its no-read fast paths never dereference;
- strncpy required addressability for source bytes after an already-observed
  NUL even though those output bytes are zero padding.

Batch both fixes with focused regressions and refreshed release provenance.

## Non-goals

Do not rewrite historical evidence to pretend it used a newer source, broaden
the accepted Android namespace/search boundary, or claim the off-ref lineage is
validated.
