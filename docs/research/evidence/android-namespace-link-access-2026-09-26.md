# Android linked-namespace SONAME accessibility — 2026-09-26

## Question

What is the smallest Android linker-namespace behavior needed after the
requester-aware feature-028 platform-provider seam?

## AOSP release baseline

The project compatibility baseline follows `android-latest-release`, which resolves to `android17-release` as of 2026-09-27. Android 17.0.0 r1 linker sources are used for the exact parser contract below.

## AOSP evidence

Bionic linker configuration models named namespaces and direct namespace links.
For each link, configuration chooses exactly one of:

- an explicit `shared_libs` list; or
- `allow_all_shared_libs=true`.

AOSP rejects a link that supplies neither mode and rejects a link that combines
both modes.

The runtime linker then checks the candidate SONAME through the selected
namespace link before allowing lookup/load from that linked namespace.
`android_namespace_link_t::is_accessible` accepts a SONAME when the link is
allow-all or the SONAME is present in the link's shared-library set.

Primary sources:

- AOSP linker configuration format:
  https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/linker/ld.config.format.md
- AOSP linker configuration parser:
  https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/linker/linker_config.cpp
- AOSP linked-namespace lookup:
  https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/linker/linker.cpp
- AOSP namespace-link accessibility type:
  https://android.googlesource.com/platform/bionic/+/refs/tags/android-17.0.0_r1/linker/linker_namespaces.h

## Bounded project consequence

Feature 029 adopts only the accessibility decision needed by the current
provider seam:

1. bind an opaque loaded-object requester identity to one configured namespace;
2. treat requests from the configured platform namespace itself as accessible;
3. otherwise require one direct link from requester namespace to the platform
   namespace;
4. accept either allow-all or an exact requested SONAME in the link list;
5. reject ambiguous/malformed matching configuration.

The model deliberately does not infer namespace membership from paths and does
not implement namespace search paths, permitted paths, filesystem/APK lookup,
RUNPATH/RPATH, LD_LIBRARY_PATH, preload/RTLD flags, or transitive namespace-link
search.

## Evidence limits

This research maps the direct-link SONAME gate, not Android's full linker
namespace implementation. Project requester identities remain opaque bytes and
namespace configuration remains caller-owned.


## Alignment result — 2026-09-27

Android 17's exact linker parser still requires each configured namespace link
to choose one accessibility mode: non-empty `shared_libs`, or
`allow_all_shared_libs=true`; it rejects neither/both. Feature 029's bounded
direct-link policy matches that rule. Android 17 also retains search/permitted
paths and other namespace behavior that feature 029 explicitly leaves outside
its current scope.
