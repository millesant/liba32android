# Public C API quick start

liba32android exposes a small versioned C API in
`include/liba32android/liba32android.h`. The public surface intentionally covers
runtime lifetime, logical guest memory, bounded ARM/Thumb execution, SVC traps,
and structured diagnostics while private ELF/compatibility policy continues to
evolve.

## Build and install

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/build-install"
cmake --build build --target liba32android --parallel
cmake --install build
```

The staged public files are:

```text
build-install/include/liba32android/liba32android.h
build-install/lib/liba32android.so
```

## Compile an external caller

The repository's executable public-contract regression is
`tests/runtime/public_embedding_api.c`. Compile it exactly as an external
consumer would:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I"$PWD/build-install/include" \
  tests/runtime/public_embedding_api.c \
  -L"$PWD/build-install/lib" \
  -Wl,-rpath,"$PWD/build-install/lib" \
  -la32android \
  -o build/public_embedding_external

build/public_embedding_external
```

A successful run includes:

```text
public.embedding.api_version=1
public.embedding.arm_result=42
public.embedding.svc=0x12
public.embedding.structured_error=PASS
public.embedding.status=PASS
```

## API flow

A minimal embedder typically:

1. checks `liba32android_api_version()`;
2. creates an opaque runtime;
3. queries runtime page size;
4. maps logical 32-bit guest memory;
5. writes guest code/data and applies final permissions;
6. fills a `liba32android_execution_request`;
7. executes with a finite instruction budget;
8. inspects registers, stop-PC/SVC flags, and structured error text;
9. unmaps guest memory and destroys the runtime.

Guest addresses are always 32-bit logical values. Do not pass host pointers as
guest addresses.

## ABI policy

`LIBA32ANDROID_API_VERSION` is currently `1`. Request/result structures carry a
`struct_size` field so compatible future versions can append fields without
requiring callers to reinterpret private C++ objects.

The public ABI does not currently expose ELF loader/linker or Android
compatibility policy objects. Those remain private until their contracts are
mature enough to support intentionally.
