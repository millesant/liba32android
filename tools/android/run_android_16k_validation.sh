#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: run_android_16k_validation.sh PROBE RUNTIME_SMOKE LIBA32ANDROID [OUTPUT_DIR]

Requires one running AArch64 Android emulator/device that reports PAGE_SIZE=16384.
ANDROID_SERIAL may select a specific adb target. ADB may override the adb executable.
EOF
}

if [[ $# -lt 3 || $# -gt 4 ]]; then
    usage
    exit 2
fi

probe=$1
runtime_smoke=$2
runtime_library=$3
output_dir=${4:-android-16k-validation}
remote_dir=/data/local/tmp/liba32android-16k-validation
adb_bin=${ADB:-adb}

for file in "$probe" "$runtime_smoke" "$runtime_library"; do
    if [[ ! -f "$file" ]]; then
        echo "missing input file: $file" >&2
        exit 2
    fi
done

if ! command -v "$adb_bin" >/dev/null 2>&1; then
    echo "adb executable not found: $adb_bin" >&2
    exit 2
fi

adb_args=()
if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    adb_args=(-s "$ANDROID_SERIAL")
fi

adb_cmd() {
    "$adb_bin" "${adb_args[@]}" "$@"
}

capture_shell() {
    local name=$1
    local command=$2
    echo "== $name =="
    adb_cmd shell "$command" | tr -d '\r' | tee "$output_dir/$name.log"
}

mkdir -p "$output_dir"

adb_cmd get-state >/dev/null

page_size=$(adb_cmd shell getconf PAGE_SIZE | tr -d '\r\n')
if [[ "$page_size" != "16384" ]]; then
    echo "expected 16384-byte Android page size, got: $page_size" >&2
    exit 1
fi

arch=$(adb_cmd shell uname -m | tr -d '\r\n')
if [[ "$arch" != "aarch64" ]]; then
    echo "expected AArch64 Android target, got: $arch" >&2
    exit 1
fi

{
    echo "page_size=$page_size"
    echo "arch=$arch"
    echo "android.runtime_sdk=$(adb_cmd shell getprop ro.build.version.sdk | tr -d '\r\n')"
    echo "android.release=$(adb_cmd shell getprop ro.build.version.release | tr -d '\r\n')"
    echo "kernel.release=$(adb_cmd shell uname -r | tr -d '\r\n')"
    echo "kernel.version=$(adb_cmd shell uname -v | tr -d '\r\n')"
} | tee "$output_dir/environment.log"

adb_cmd shell "rm -rf '$remote_dir' && mkdir -p '$remote_dir'"
adb_cmd push "$probe" "$remote_dir/android_address_space_probe" >/dev/null
adb_cmd push "$runtime_smoke" "$remote_dir/android_runtime_smoke" >/dev/null
adb_cmd push "$runtime_library" "$remote_dir/liba32android.so" >/dev/null
adb_cmd shell "chmod 755 '$remote_dir/android_address_space_probe' '$remote_dir/android_runtime_smoke'"

capture_shell probe \
    "$remote_dir/android_address_space_probe --log-file $remote_dir/address-space-probe.log"
capture_shell probe-generated-code \
    "$remote_dir/android_address_space_probe --execute-generated-code --log-file $remote_dir/address-space-probe-jit.log"
capture_shell runtime-smoke \
    "LD_LIBRARY_PATH=$remote_dir $remote_dir/android_runtime_smoke --log-file $remote_dir/runtime-smoke.log"
capture_shell runtime-fastmem-fallback \
    "LD_LIBRARY_PATH=$remote_dir $remote_dir/android_runtime_smoke --log-file $remote_dir/runtime-fastmem-fallback.log --exercise-fastmem-fault"

grep -F 'page_size=16384' "$output_dir/probe.log"
grep -F 'jit_wx.result_check=PASS' "$output_dir/probe-generated-code.log"
grep -F 'page_size=16384' "$output_dir/runtime-smoke.log"
grep -F 'a32.return42.status=PASS' "$output_dir/runtime-smoke.log"
grep -F 'a32.memory.fastmem_direct=true' "$output_dir/runtime-smoke.log"
grep -F 'a32.memory.status=PASS' "$output_dir/runtime-smoke.log"
grep -F 'runtime_smoke.complete=true' "$output_dir/runtime-smoke.log"
grep -F 'a32.fastmem_fault.memory_fault=true' "$output_dir/runtime-fastmem-fallback.log"
grep -F 'a32.fastmem_fault.data_read_callbacks=' "$output_dir/runtime-fastmem-fallback.log"
grep -F 'a32.fastmem_fault.status=PASS' "$output_dir/runtime-fastmem-fallback.log"
grep -F 'runtime_smoke.complete=true' "$output_dir/runtime-fastmem-fallback.log"

echo "android_16k_validation.status=PASS"
echo "android_16k_validation.output_dir=$output_dir"
