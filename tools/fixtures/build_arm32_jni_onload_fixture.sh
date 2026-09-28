#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <ndk-root> <output-so>" >&2
    exit 2
fi

ndk_root=$1
output=$2
host_tag=${NDK_HOST_TAG:-linux-x86_64}
clang="$ndk_root/toolchains/llvm/prebuilt/$host_tag/bin/armv7a-linux-androideabi26-clang"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
source="$repo_root/tests/compat/fixtures/a32_jni_onload.c"

if [[ ! -x "$clang" ]]; then
    echo "ARMv7 Android clang not found: $clang" >&2
    exit 1
fi
if [[ ! -f "$source" ]]; then
    echo "fixture source not found: $source" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$output")"

"$clang" \
    -shared \
    -fPIC \
    -O2 \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fno-unwind-tables \
    -fno-asynchronous-unwind-tables \
    -nostdlib \
    -marm \
    -Wl,--build-id=none \
    -Wl,--no-undefined \
    -Wl,--hash-style=sysv \
    -Wl,-z,max-page-size=16384 \
    -Wl,-soname,libfixture_jni_onload.so \
    -o "$output" \
    "$source"
