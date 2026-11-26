#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <ndk-root> <output-dir>" >&2
    exit 2
fi

ndk_root=$1
output_dir=$2
host_tag=${NDK_HOST_TAG:-linux-x86_64}
clang="$ndk_root/toolchains/llvm/prebuilt/$host_tag/bin/armv7a-linux-androideabi26-clang"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
provider_source="$script_dir/../../tests/elf/fixtures/arm32_versioned_provider.c"
consumer_source="$script_dir/../../tests/elf/fixtures/arm32_versioned_consumer.c"
version_script="$script_dir/../../tests/elf/fixtures/arm32_versioned_provider.map"
provider="$output_dir/liba32android_versioned_provider.so"
consumer="$output_dir/liba32android_versioned_consumer.so"

if [[ ! -x "$clang" ]]; then
    echo "ARMv7 Android clang not found: $clang" >&2
    exit 1
fi
for source in "$provider_source" "$consumer_source" "$version_script"; do
    if [[ ! -f "$source" ]]; then
        echo "fixture source not found: $source" >&2
        exit 1
    fi
done

mkdir -p "$output_dir"

common=(
    -shared
    -fPIC
    -ffreestanding
    -fno-stack-protector
    -nostdlib
    -Wl,--build-id=none
    -Wl,--no-undefined
    -Wl,-z,now
    -Wl,-z,max-page-size=16384
)

"$clang"     "${common[@]}"     -Wl,-soname,liba32android_versioned_provider.so     -Wl,--version-script="$version_script"     -o "$provider"     "$provider_source"

"$clang"     "${common[@]}"     -Wl,-soname,liba32android_versioned_consumer.so     -o "$consumer"     "$consumer_source"     -L"$output_dir"     -Wl,--no-as-needed     -la32android_versioned_provider
