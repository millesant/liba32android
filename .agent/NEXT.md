# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-aeabi-dso-binding-association` is DONE at
`74b356f36d1cc2cd3a8326bc52b135e38e12e50a`.

`post-roadmap-arm32-libdl-load-policy` is IMPLEMENTED on `bleeding`.
Exact-head required checks are its current acceptance gate.

The ARM32 libdl surface now uses bionic LP32 RTLD values, preserves eager
relocation under RTLD_LAZY input, creates exact ownership roots for successful
named opens, supports resident-only NOLOAD, monotonically promotes Global and
NODELETE policy, recognizes ARM32 RTLD_DEFAULT/RTLD_NEXT sentinels, and retains
linked Global/NODELETE roots on final close in both exact and physical close
paths.

After terminal success, move outward to concrete APK/ZIP native-library byte
acquisition. Feature 048 already has requester-scoped virtual paths such as
`base.apk!/lib/armeabi-v7a/libvlc.so`, and the accepted filesystem source
already proves the source abstraction with extracted directories. Add the
smallest bounded archive-aware `A32AndroidLibrarySource` that can consume
those APK-style virtual paths and return owned ELF bytes under the existing
path/image ceilings.

Keep package-manager/AssetManager discovery, signature verification, APK
installation policy, explicit-path dlopen, RTLD_NEXT, tombstone compaction, and
JNI/graphics/audio separate. Prefer existing platform/library dependencies over
introducing a custom general ZIP framework; support only the archive features
proven necessary by the supplied APK evidence.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
