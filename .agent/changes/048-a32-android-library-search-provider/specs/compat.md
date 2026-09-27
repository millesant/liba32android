# Compatibility spec delta — feature 048

Add a requester-aware Android application library search provider above the
generic ELF provider seam.

The provider searches only bare names under finite ordered roots bound to exact
requester identities. Candidate paths and image sizes are caller-bounded.
Concrete filesystem/APK/archive I/O is delegated to a caller-owned source.
NotFound falls through, hard failure stops, and success requires valid
non-empty identity/image data.

Real ARM32 integration must resolve one DT_NEEDED child exclusively through an
APK-style virtual root, eagerly relocate the root, and execute into the searched
child.

Do not implement explicit paths, RUNPATH/RPATH, LD_LIBRARY_PATH, permitted
paths, transitive namespace search, ZIP parsing, dynamic dlopen acquisition, or
unload semantics in this feature.
