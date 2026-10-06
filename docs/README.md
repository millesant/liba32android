# Documentation

**Language:** English | [Português (Brasil)](README.pt-BR.md)

This directory separates current architecture/development guidance from
research and historical evidence.

## Start here

- [Public C API quick start](development/public-api-quickstart.md)
- [Build and test](development/build-and-test.md)
- [Repository layout](development/repository-layout.md)
- [Architecture index](architecture/README.md)
- [Project roadmap](../ROADMAP.md)
- [Retired pre-v7 specifications](history/pre-v7-specs.md)
- [Contributing](../CONTRIBUTING.md)

## Architecture

- [CPU engine](architecture/cpu-engine.md)
- [A32 host-service dispatch](architecture/a32-service-dispatch.md)
- [Public C embedding API](architecture/public-embedding-api.md)
- [ARM32 JNI compatibility](architecture/a32-jni.md)
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

### Compatibility architecture

- [Logical guest-thread context](architecture/a32-logical-thread-context.md)
- [Pthread lifecycle](architecture/a32-pthread-lifecycle.md)
- [Pthread synchronization](architecture/a32-pthread-sync.md)
- [Signal compatibility](architecture/a32-signal-compat.md)
- [Scheduler/priority compatibility](architecture/a32-scheduler-compat.md)
- [Libc memory/string services](architecture/a32-libc-memory-string-service.md)
- [Libc integer parsing](architecture/a32-libc-integer-service.md)
- [Libc heap](architecture/a32-libc-heap.md)
- [Libc clock_gettime](architecture/a32-libc-clock.md)
- [Partial ARM32 libc shim](architecture/a32-libc-memory-string-shim.md)
- [Android liblog services and shim](architecture/android-log-write-shim.md)
- [Android platform catalog](architecture/android-platform-catalog-provider.md)
- [Android namespace access policy](architecture/android-namespace-access-policy.md)
- [C++ finalization](architecture/a32-cxa-finalize.md)
- [Resident dlclose transaction](architecture/a32-libdl-close-transaction.md)

## Development

- [Public C API quick start](development/public-api-quickstart.md)
- [Repository layout](development/repository-layout.md)
- [Build and test](development/build-and-test.md)
- [Diagnostics](diagnostics.md)
- [Security policy](../SECURITY.md)

## Research and evidence

`contracts/` contains accepted current engineering contracts.
`research/` contains research notes and environment-specific evidence. These
records are useful context, but they do not replace accepted contracts or
exact-revision test/CI evidence.

Real third-party binaries used for compatibility research are evidence inputs;
they are not checked into the repository unless redistribution rights explicitly
permit it.
