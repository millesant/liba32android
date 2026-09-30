# ARM32 JNI byte-array element entrypoint evidence — 2026-09-30

## Artifact

Supplied `libfmod.so` is an ELF32 little-endian ARM EABI5 shared object.

SHA-256:

`982e994c46a7f797fbd6e10df31a98d544c2bf117fe33c2292f4d6cc454a6544`

The dynamic symbol table exports
`Java_org_fmod_MediaCodec_fmodReadAt` at `0x000cf784`.

## Observed machine code

Inside that export, the function loads the JNIEnv function table and then:

- loads a function pointer from byte offset `0x2e0` and calls it with the
  incoming byte-array handle plus a null `isCopy` pointer;
- later loads a function pointer from byte offset `0x300` and calls it with
  the same array handle, returned elements pointer, and release mode 0.

JNIEnv entries are 32-bit words on ARMv7:

- `0x2e0 / 4 = 184` → `GetByteArrayElements`;
- `0x300 / 4 = 192` → `ReleaseByteArrayElements`.

The same binary's JNI_OnLoad also uses already-supported GetEnv, FindClass, and
NewGlobalRef entries, which is consistent with the current runtime surface.

## Boundary

This observation proves only the two element-lease entrypoints used by this
function. It does not establish NewByteArray, byte-region APIs, other primitive
array families, or Java-side MediaCodec behavior.
