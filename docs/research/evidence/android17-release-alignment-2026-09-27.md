# Android 17 release compatibility alignment — 2026-09-27

## Baseline resolution

Current Android compatibility research follows the official AOSP
`android-latest-release` manifest rather than unqualified project
`main`/`master`. At manifest commit
`ad156f32caaa06dae91c02d443f6a8fe210eaa54`, `default.xml` selects
`android17-release`.

For exact stable source comparisons this audit uses Android 17.0.0 release 1.
The bionic tag resolves to commit
`06356e41c5ed7b12220b24c05ba4fdb873b126a7`.

Primary baseline:

- https://android.googlesource.com/platform/manifest/+/refs/heads/android-latest-release/default.xml
- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1
- https://android.googlesource.com/platform/system/logging/+/refs/tags/android-17.0.0_r1

## Audited compatibility surfaces

### Linked namespaces — aligned within the feature-029 boundary

Android 17's linker configuration keeps direct namespace links and two
mutually-exclusive library-access modes: a non-empty `shared_libs` set or
`allow_all_shared_libs=true`. The parser rejects a configured link with
neither mode and a link that combines both.

Feature 029 models this direct SONAME gate. Android search/permitted paths,
filesystem/APK lookup, environment/preload behavior and other linker policy
remain explicit later work.

Exact source:

https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/linker/linker_config.cpp

### atoi/strtol — aligned within the feature-035 boundary

Android 17 bionic implements `atoi` through decimal `strtol`. The release
source preserves base 0 or 2-36 validation, guarded 0x and 0b prefixes, leading
C whitespace/sign, original-input end pointer when no digits are consumed,
continued digit consumption after overflow, and EINVAL/ERANGE behavior modeled
by feature 035.

Exact source:

https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/bionic/strtol.cpp

### __errno — aligned abstraction

Android 17 bionic's `__errno()` returns the calling bionic thread's errno
storage. Features 037/038 intentionally model the observable ABI — one logical
guest `int*` per current guest thread/execution context — without copying
bionic's private pthread/TLS layout.

Exact sources:

- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/include/errno.h
- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/bionic/__errno.cpp

### memmem — implementation correction required

Android 17 builds the OpenBSD/musl-derived `memmem`. Its observable fast paths
return the haystack for an empty needle and return null when the haystack is
shorter than a non-empty needle before dereferencing the byte ranges.

The prepared feature-033 service had the correct result values but performed
full 32-bit guest-range validation before those no-read fast paths. That could
turn a call whose pointers must not be dereferenced into `Failed`.

Feature 039 keeps configured length ceilings first, then performs the
empty-needle and short-haystack no-read decisions, and validates guest ranges
only if a real byte search follows. A regression supplies a nominal wrapping
haystack with an empty needle and requires the original haystack pointer.

Android 17 build/source references:

- https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/libc/Android.bp
- https://android.googlesource.com/platform/bionic/+/422b2044ea4b5ce7375330814074d141423c32f9

### strncpy — implementation correction required

The prepared feature-033 service already stopped source reads at NUL and padded
the destination with zeros, but it also prevalidated the full nominal
`source .. source+count` range. That is stricter than the function's access
behavior: after NUL, the remaining count bytes are destination padding and
there are no corresponding source reads.

Feature 039 removes the full source-range precheck. It still validates the full
destination count range before mutation, while each actually consumed source
byte is checked by the existing bounded GuestMemory read.

A sparse `MappedGuestMemory` regression maps `{'A', '\0'}` at guest address
`0xfffffffe`, requests `strncpy(..., 4)`, and requires
`{'A', 0, 0, 0}` at a valid destination. The old implementation rejects this
call before observing the early NUL.

### __android_log_write — no ABI correction found

The current bridge continues to model the stable three-machine-word ABI
`int __android_log_write(int, const char*, const char*)`, AAPCS32 r0-r2
arguments, r0 result, bounded guest strings, and null-tag preservation. The
Android 17 system/logging project retains the release API surface; no evidence
in this audit requires a feature-026 ABI change.

### Accepted linker/lifecycle/address-space evidence

Earlier accepted evidence records cite the then-current bionic `master` for
constructor/destructor order, version tracking, global-group formation, and the
kernel UAPI declaration of `MAP_FIXED_NOREPLACE`. Those records remain
historical evidence rather than being rewritten retroactively. Feature 039
establishes Android 17 as the current comparison baseline; no implementation
change was identified in those already-bounded project contracts during this
audit. Runtime kernel probing remains authoritative for
`MAP_FIXED_NOREPLACE` behavior regardless of header availability.

## Future-source rule

For "current Android" work:

1. resolve `android-latest-release`;
2. record the selected release branch/ref;
3. use an exact release/tag source for semantic contracts when available;
4. use unqualified `main` only as a forward-looking comparison.

Historical evidence may preserve the source revision it actually used; current
contracts should not silently inherit an unqualified branch.

## Validation boundary

Feature 039 is prepared off-ref. The implementation/spec/documentation changes
and new regressions are NOT passing evidence until the prepared lineage is
integrated and the relevant tests actually run.
