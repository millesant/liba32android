# Next Work

The numbered 011-049 roadmap is COMPLETE.

`post-roadmap-arm32-libdl-load-policy` is DONE at
`431904d7b0b0880691b503d76d69c24a18b90960`.

Start `post-roadmap-android-apk-library-source`.

Feature 048 already constructs exact requester-scoped virtual candidates such as
`/path/base.apk!/lib/armeabi-v7a/libvlc.so`. The accepted
`A32FilesystemLibrarySource` covers extracted regular-file roots. Add the
smallest bounded archive-aware `A32AndroidLibrarySource` implementation that
opens the archive path before `!/`, finds the exact ZIP entry after it, and
returns owned native-library bytes under the existing source contract.

The supplied VLC APK proves the required archive subset: about 67.3 MB, 2,617
entries, and all four ARMv7 native libraries use ZIP method 8 / DEFLATE. Support
stored entries too for ordinary APK compatibility, but reject encryption,
multi-disk archives, ZIP64 sentinels, unsupported compression, malformed
central/local headers, duplicate exact names, CRC mismatch, and every
caller-selected resource ceiling before publishing bytes.

Prefer system/NDK zlib for raw DEFLATE. Do not add package-manager/AssetManager
discovery, extraction-to-disk, archive rewriting, signature verification,
split-APK policy, or a generic ZIP framework.

Focused validation should cover stored + deflated success, exact missing entry,
malformed virtual path, entry/central-directory/archive/image ceilings,
unsupported/encrypted entries, CRC failure, and composition with the existing
requester-scoped Android search provider. Add one supplied-VLC APK evidence
probe if it can remain deterministic without checking the APK into the repo.

If a later step materially needs the user's Linux machine, stop beforehand and
provide exact commands and expected output.
