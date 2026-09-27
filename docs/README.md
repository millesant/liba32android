# Documentation

This directory separates current architecture/development guidance from research and historical evidence.

## Architecture

- [Architecture index](architecture/README.md)
- [CPU engine](architecture/cpu-engine.md)
- [A32 host-service dispatch](architecture/a32-service-dispatch.md)
- [Public C embedding API](architecture/public-embedding-api.md)
- [ARM32 resident libdl compatibility](architecture/a32-libdl.md)
- [ARM32 shared libm compatibility](architecture/a32-libm.md)
- [Android application library search](architecture/a32-android-library-search.md)
- [ELF32 loader](architecture/elf32-loader.md)
- [ELF32 dynamic metadata](architecture/elf32-dynamic.md)
- [Linker metadata](architecture/elf32-linker-metadata.md)
- [Lifecycle arrays](architecture/elf32-lifecycle.md)
- [Linker strings](architecture/elf32-linker-strings.md)
- [Dependency resolution](architecture/elf32-dependency-resolution.md)
- [Dependency loading](architecture/elf32-dependency-loading.md)
- [Symbol resolution](architecture/elf32-symbol-resolution.md)
- [Symbol versioning](architecture/elf32-symbol-versioning.md)
- [Relocation](architecture/elf32-relocation.md)
- [Real fixture execution](architecture/elf32-execution.md)
- [GNU RELRO](architecture/elf32-relro.md)

## Development

- [Repository layout](development/repository-layout.md)
- [Build and test](development/build-and-test.md)
- [Diagnostics](diagnostics.md)

## Research and evidence

`research/` contains research notes and environment-specific evidence. These records are useful context, but they do not replace accepted current contracts in `.agent/specs/` or exact-revision validation in `.agent/STATE.md`.
