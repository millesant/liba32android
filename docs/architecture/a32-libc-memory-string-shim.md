# ARM32 guest libc memory/string shim/provider path

Status: features 032/034 implementation prepared OFF-REF; exact-head validation NOT RUN

## Goal

Connect feature 030's bounded libc memory/string host services to the real ELF
dependency/symbol/relocation path using one reproducible partial ARM32
`libc.so` compatibility DSO.

The feature is deliberately partial. Feature 032 introduced seven symbols; feature 034 extends the same DSO to ten by adding the three feature-033 copy/search services.

## Guest stubs

The freestanding ARM-mode shim has SONAME `libc.so` and exports:

- `memcpy` -> SVC `0xA1`
- `memset` -> SVC `0xA2`
- `memcmp` -> SVC `0xA3`
- `memchr` -> SVC `0xA4`
- `strlen` -> SVC `0xA5`
- `strcmp` -> SVC `0xA6`
- `strncmp` -> SVC `0xA7`\n- `memmem` -> SVC `0xA8`\n- `strcpy` -> SVC `0xA9`\n- `strncpy` -> SVC `0xAA`

Each function consists only of its shared feature-030 SVC followed by
`bx lr`. The guest and host consume the same preprocessor-safe service-ID
header.

## Provider

`make_a32_libc_memory_string_shim_catalog_entry` creates a borrowed exact-name
catalog entry for SONAME `libc.so` with stable identity
`liba32android-compat-libc-memory-string`.

The real integration places that entry in feature 031's finite platform
catalog. Feature 029 namespace policy exposes exact `libc.so` from the
consumer's `app` namespace to the `platform` namespace before dependency
loading may acquire the shim.

## Real consumer

A freestanding companion DSO imports all ten symbols through ordinary function calls with builtins disabled. It therefore provides a real
`DT_NEEDED libc.so` edge plus eager ARM JUMP_SLOT relocations.

After graph load and relocation, the integration executes every exported
fixture wrapper against logical guest buffers/strings. Each call crosses:

consumer PLT -> partial libc shim -> SVC -> feature-025 registry ->
feature-030/033 service -> shim return -> consumer return -> requested stop PC.

## Limits

This DSO is not a replacement for Android's real libc. It does not provide
allocation, pthreads, file/socket/stdio, errno, process startup, dynamic loader,
math, locale, signals, or any other libc surface. The supplied FMOD/VLC
binaries still cannot be claimed loadable from this ten-symbol shim alone.
