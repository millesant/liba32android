# Supplied ARM32 Android logging imports — 2026-09-26

## Scope

Bounded static `readelf` inspection of the user-supplied ARM32 FMOD library
and the `lib/armeabi-v7a/*.so` members of the supplied VLC Android APK. The
third-party binaries are evidence inputs only and are not vendored into this
repository.

Artifact identities:

- `libfmod.so` SHA-256
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`;
- `VLC-Android-3.7.2-Beta-2-all-20260925-0117.apk` SHA-256
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`.

## Observed undefined Android log symbols

`libfmod.so`:

- `__android_log_write`.

VLC ARMv7 members:

- `libvlc.so`: `__android_log_print`, `__android_log_vprint`,
  `__android_log_write`;
- `libvlcjni.so`: `__android_log_print`;
- `libmla.so`: `__android_log_print`;
- `libc++_shared.so`: no `__android_log_*` undefined import observed.

Both the supplied FMOD library and VLC's `libvlc.so` therefore require
`__android_log_write`.

## Dependency context

The supplied FMOD library declares `liblog.so` among its `DT_NEEDED`
entries. VLC's ARMv7 `libvlc.so` also declares `liblog.so`.

This makes `__android_log_write` the narrowest shared concrete logging import
observed across the two supplied targets. It is a better next compatibility
probe than implementing the broader `__android_log_print` /
`__android_log_vprint` surface merely because those symbols also appear in
VLC.

## Limitations

This is static import/dependency evidence only. It does not establish the
platform function signature, calling convention details, runtime call
frequency, acceptable return behavior, logging side effects, or compatibility.
Those contracts must be sourced from authoritative Android API/ABI material
before implementation.


## 2026-10-05 follow-up selection

After the shared write path was accepted, the remaining supplied VLC
`liblog.so` imports became the next bounded logging gap. Issue #72 therefore
selects exactly `__android_log_print` and `__android_log_vprint` rather than
expanding to unrelated Android log APIs.

Authoritative Android declarations and AAPCS32 vararg/`va_list` details are
captured in
`docs/research/evidence/android-log-format-abi-2026-10-05.md`.
