# ARM32 libdl target evidence — 2026-09-27

## Supplied artifacts

- `libfmod.so` SHA-256:
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`
- VLC Android APK SHA-256:
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`

Bounded `readelf -WsW` inspection shows FMOD imports:

- `dlopen`
- `dlsym`
- `dlclose`
- `dlerror`

The extracted VLC ARMv7 set uses the same four-function surface and additionally
imports `dladdr`. In the inspected VLC set, `libmla.so` and `libvlc.so`
import all five; `libc++_shared.so` imports `dladdr`; `libvlcjni.so`
imports `dladdr` and `dlsym`.

Static strings in the supplied binaries include dynamically relevant SONAMEs
such as `libOpenSLES.so`, `libandroid.so`, `libEGL.so`,
`libGLESv2.so`, `libmediandk.so`, and application-local VLC DSOs.

## Feature boundary

Feature 046 deliberately implements only libdl operations over objects that are
already resident in the caller-owned persistent `Elf32LinkMap`. This is enough
to establish the ARM32 ABI/shim, opaque handle lifetime, exact symbol lookup,
address-to-object/symbol reporting, and dlerror behavior without smuggling
Android filesystem/search or dynamic-load lifecycle policy into the
compatibility service.

A later filesystem/search/embedding layer may preload or acquire additional
objects before calling this service. True missing-object `dlopen`, relocation
and constructor orchestration for newly acquired roots, unload/destructors,
RTLD_GLOBAL/NOLOAD/NODELETE, and persistent lifecycle called-state remain
separate.
