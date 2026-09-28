# Compatibility spec delta — bounded APK native-library catalog

`A32ApkLibrarySource::catalog` enumerates the finite app-local native-library
membership for one caller-selected APK and relative ABI directory.

The operation reuses the exact same private ZIP32 archive-open and central-
directory parser as exact-entry `load`. There is one structural validation path
for single-disk/ZIP32/entry-count/central-directory/entry-name bounds.

Catalog options require non-zero caller ceilings for:

- maximum discovered libraries;
- maximum bytes per SONAME;
- maximum total bytes across all published SONAMEs; and
- maximum ABI-directory bytes.

The existing APK source options continue to bound archive bytes, ZIP entry
count, central-directory bytes, entry-name bytes, and virtual/path bytes.

The ABI directory must be relative, NUL-free, backslash-free, `!`-free, have
no leading/trailing slash, and contain no empty, dot, or dot-dot components.

Only direct children below `<abi-directory>/` are considered. Nested entries
are ignored. A candidate basename must be non-empty, contain no slash,
backslash, or NUL, and end exactly in `.so`. Non-.so entries are ignored.

Any qualifying SONAME that exceeds the per-name, total-name, or catalog-count
ceiling fails the operation. Duplicate qualifying SONAMEs fail as ambiguous.
Successful output owns all strings and is sorted lexicographically.

A missing APK returns NotFound. Invalid options/path policy or any malformed,
truncated, multi-disk, ZIP64-sentinel, or resource-invalid central-directory
state returns Failed through the shared parser.

The catalog does not read/decompress native payload bytes and does not inspect
ELF DT_SONAME. ZIP entry basenames define application membership for this
caller-selected ABI directory.

Catalog output may be converted to string views and passed directly into
`A32AndroidApkRuntimeBootstrap`. The caller still selects the initial root
SONAME explicitly.

ABI auto-detection, manifest/root selection, split APK merging, package-manager
or AssetManager discovery, signatures, JNI startup, graphics/audio, patching,
and device deployment remain separate.
