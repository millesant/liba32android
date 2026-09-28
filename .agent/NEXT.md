# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-android-apk-library-source` is DONE at
`4c3cca6e8bd9b04c4995e82b83c5976f9be52763`.

`post-roadmap-android-apk-runtime-bootstrap` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The bootstrap now accepts a caller-supplied APK path, exact ABI directory,
finite complete app-local SONAME set, platform provider, and persistent runtime
state. It owns the APK/search/provider composition and delegates initial root
load plus later named app-local dlopen to the accepted
`A32LibDlOpenTransaction`.

The real ARM32 workflow places both fixture DSOs in one deterministic DEFLATED
APK, starts with an empty link map, bootstraps the root from archive bytes,
loads the child transitively, executes the relocated call, and obtains a later
child handle through the same transaction.

After terminal success, remove the next major piece of manual configuration:
add a bounded APK native-library catalog operation for one caller-selected ABI
directory. It should enumerate exact direct `*.so` entries under
`<apk>!/<abi>/`, enforce caller entry/name/central-directory/catalog ceilings,
reject ambiguous duplicate SONAMEs, and produce the finite SONAME set consumed
by the bootstrap. Reuse the existing ZIP32 parser structures rather than adding
a second archive parser.

Keep ABI auto-detection, manifest/root-library selection, split APKs,
package-manager/AssetManager discovery, signatures, JNI/graphics/audio,
automatic app patching, and device deployment separate. The caller should still
select the APK, ABI directory, and initial root SONAME after catalog discovery.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
