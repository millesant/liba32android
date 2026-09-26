# Android linked-namespace SONAME accessibility — 2026-09-26

## Question

What is the smallest Android linker-namespace behavior needed after the
requester-aware feature-028 platform-provider seam?

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
  https://android.googlesource.com/platform/bionic/+/show/master/linker/ld.config.format.md
- AOSP linker configuration parser:
  https://android.googlesource.com/platform/bionic/+/ba1151c761534392faef9b1148aa554c671e0d91/linker/linker_config.cpp
- AOSP linked-namespace lookup:
  https://android.googlesource.com/platform/bionic/+/master/linker/linker.cpp
- AOSP namespace-link accessibility type:
  https://android.googlesource.com/platform/bionic/+/bcfe3cf/linker/linker_namespaces.h

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
