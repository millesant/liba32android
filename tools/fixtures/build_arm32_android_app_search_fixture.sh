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
root_source="$repo_root/tests/compat/fixtures/a32_android_app_search_root.c"
child_source="$repo_root/tests/compat/fixtures/a32_android_app_search_child.c"
root="$output_dir/libfixture_app_root.so"
child="$output_dir/libfixture_app_child.so"

if [[ ! -x "$clang" ]]; then
    echo "ARMv7 Android clang not found: $clang" >&2
    exit 1
fi
for source in "$root_source" "$child_source"; do
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

"$clang" "${common[@]}"     -Wl,-soname,libfixture_app_child.so     -o "$child" "$child_source"

"$clang" "${common[@]}"     -Wl,-soname,libfixture_app_root.so     -o "$root" "$root_source"     -Wl,--no-as-needed "$child"
