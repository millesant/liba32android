# Design — bounded APK native-library catalog

Reuse the accepted APK ZIP32 parser instead of adding archive logic.

Refactor the private APK source implementation around one bounded archive-open
and central-directory reader that returns owned central metadata for every ZIP
entry after validating the accepted single-disk ZIP32 structure and the source
ceilings. Exact-entry load then searches that same directory representation and
continues with the accepted local-header/payload/DEFLATE/CRC path.

Add one catalog operation on `A32ApkLibrarySource`:

- input: exact APK filesystem path and one caller-selected relative ABI
  directory such as `lib/armeabi-v7a`;
- output: owned bare SONAME strings only;
- filtering: direct children of that ABI directory only, basename ending in
  `.so`, no slash/backslash/NUL in the basename;
- bounds: caller-selected maximum catalog libraries, per-SONAME bytes, total
  SONAME bytes, and ABI-directory bytes, in addition to the source's existing
  archive/entry/central/name ceilings;
- ambiguity: duplicate direct SONAMEs fail the whole operation;
- order: lexicographically sorted for deterministic bootstrap input.

The catalog does not inspect ELF SONAME tags or payload bytes. ZIP path basenames
are the catalog membership source. Existing ELF loading remains responsible for
parsing each selected image when it is actually requested.

Host coverage should pass discovered catalog strings into the existing
`A32AndroidApkRuntimeBootstrap` to prove the manual SONAME list can be replaced
without changing bootstrap ownership semantics.

The caller still chooses APK, ABI directory, and initial root SONAME. Automatic
ABI/root discovery is deliberately deferred.
