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
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
shim_source="$repo_root/tests/compat/fixtures/a32_libdl_shim.S"
consumer_source="$repo_root/tests/compat/fixtures/a32_libdl_consumer.c"
provider_source="$repo_root/tests/compat/fixtures/a32_libdl_provider.c"
shim="$output_dir/libdl.so"
consumer="$output_dir/liba32android_libdl_consumer.so"
provider="$output_dir/libfixture_dl_target.so"

if [[ ! -x "$clang" ]]; then
    echo "ARMv7 Android clang not found: $clang" >&2
    exit 1
fi
for source in "$shim_source" "$consumer_source" "$provider_source"; do
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
    -fno-builtin
    -fno-stack-protector
    -nostdlib
    -marm
    -Wl,--build-id=none
    -Wl,--no-undefined
    -Wl,-z,max-page-size=16384
)

"$clang" "${common[@]}" -I"$repo_root/src"     -Wl,-soname,libdl.so     -o "$shim" "$shim_source"

"$clang" "${common[@]}"     -Wl,-soname,libfixture_dl_target.so     -o "$provider" "$provider_source"

"$clang" "${common[@]}"     -Wl,-soname,liba32android_libdl_consumer.so     -o "$consumer" "$consumer_source"     -Wl,--no-as-needed "$shim" "$provider"
