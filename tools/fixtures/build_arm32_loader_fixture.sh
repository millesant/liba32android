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
source_file="$script_dir/../../tests/elf/fixtures/arm32_loader_fixture.c"

if [[ ! -x "$clang" ]]; then
    echo "ARMv7 Android clang not found: $clang" >&2
    exit 1
fi
if [[ ! -f "$source_file" ]]; then
    echo "fixture source not found: $source_file" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$output")"

"$clang" \
    -shared \
    -fPIC \
    -ffreestanding \
    -fno-stack-protector \
    -nostdlib \
    -Wl,--build-id=none \
    -Wl,--no-undefined \
    -Wl,-soname,liba32android_loader_fixture.so \
    -Wl,-z,max-page-size=16384 \
    -o "$output" \
    "$source_file"
