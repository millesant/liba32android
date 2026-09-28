# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-arm32-libdl-load-policy` is DONE at
`431904d7b0b0880691b503d76d69c24a18b90960`.

`post-roadmap-android-apk-library-source` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The new `A32ApkLibrarySource` performs bounded exact-entry ZIP32 reads for
stored and DEFLATE entries, validates selected local/central metadata and CRC,
and composes unchanged with requester-scoped Android library search. The real
ARM32 app-search integration now loads its dependency from a deterministic
DEFLATED mini-APK before relocation/execution.

The supplied VLC APK directly motivates the supported subset: all four ARMv7
native libraries use method-8 DEFLATE in an ordinary single-disk ZIP32 archive.

After terminal success, move one layer upward instead of expanding ZIP breadth:
add caller-supplied APK/ABI bootstrap composition that can acquire an initial
application DSO from `<apk>!/lib/armeabi-v7a`, construct the requester-scoped
application-native provider plus accepted platform-provider chain, and feed the
existing persistent link-map / dynamic-dlopen machinery. This is the shortest
path toward running a real APK dependency closure without manual extraction.

Keep package-manager/AssetManager discovery, split-APK selection, signatures,
ZIP64/encrypted archives, automatic app patching, JNI/graphics/audio, and device
deployment separate until the caller-supplied APK bootstrap is proven.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
