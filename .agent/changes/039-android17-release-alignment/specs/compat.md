# Compatibility spec delta — feature 039

Current Android semantic work resolves the official `android-latest-release`
manifest first. As of 2026-09-27 it selects `android17-release`; Android
17.0.0 r1 is the stable exact-source comparison point used by this batch.

The audit preserves the existing bounded direct namespace-link policy,
Android-log-write ABI, ARM32 atoi/strtol semantics, and one-logical-guest-errno
slot abstraction.

Correct `memmem` so configured length ceilings are enforced first, then an
empty needle returns the haystack and a shorter haystack returns null before
validating or reading otherwise-unused guest byte ranges. Validate ranges only
when an actual search can dereference them.

Correct `strncpy` so count and the full destination range are bounded before
mutation, while source addressability is checked only for bytes actually read
before NUL/count. Padding after an observed NUL must not require addressability
of fictitious source bytes.

Historical evidence may keep the branch/revision it actually used. New current
Android contracts must name the resolved release/ref instead of silently using
unqualified `main` or `master`.

Feature 039 remains prepared off-ref until the feature-028 gate closes and
features 029-038 integrate before it.
