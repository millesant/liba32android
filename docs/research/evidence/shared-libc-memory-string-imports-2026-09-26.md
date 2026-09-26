# Shared ARM32 libc memory/string imports — 2026-09-26

## Scope

Bounded static `readelf` inspection of the supplied ARM32 FMOD library and the
ARMv7 `libvlc.so` extracted from the supplied VLC Android APK.

Artifact identities:

- `libfmod.so` SHA-256:
  `982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`;
- VLC APK SHA-256:
  `10da537a545d5c9aa111571bbab3d1aae4204d2c4a0d4a4cdc22efab6d71cbe5`;
- extracted ARMv7 `libvlc.so` SHA-256:
  `f92266dbe28b4e4e2477225cf6bbb56291c2e1ca7a72a4573013ebf9d52517d4`.

Commands used:

```sh
readelf -WsW libfmod.so
readelf -WsW lib/armeabi-v7a/libvlc.so
readelf -dW ...
```

Undefined symbol names were parsed from `UND` symbol rows, symbol-version
suffixes were removed, names were sorted uniquely, and the two unique sets were
intersected.

## Result

The supplied FMOD library has 106 unique non-empty undefined symbol names. VLC
ARMv7 `libvlc.so` has 572. Their intersection contains 101 names.

Both binaries import each of the following seven bounded memory/string
primitives:

- `memcpy`
- `memset`
- `memcmp`
- `memchr`
- `strlen`
- `strcmp`
- `strncmp`

Both also declare `libc.so` in `DT_NEEDED`.

These seven functions are a useful first libc host-service slice because they
operate entirely on caller-supplied memory/string arguments and do not require
allocator ownership, file descriptors, pthread state, dynamic-loader handles,
floating-point ABI handling, or process-global libc state.

## Wider common surface

The common undefined set also contains `memmem`, `strcpy`, and `strncpy`,
plus allocator calls (`malloc/calloc/realloc/free`),
`dlopen/dlsym/dlclose/dlerror`, pthread operations, file/socket I/O, stdio,
time calls, and libm functions. Those stateful surfaces have stronger
lifetime/state/ABI requirements and are intentionally not folded into the first
seven-function service.

## Limits

Static imports show that a symbol is referenced, not when or how often it is
called at runtime. This evidence does not establish that the seven-function
slice is sufficient to load either real target; both binaries require many
additional libc/platform symbols.
