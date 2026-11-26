#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: run_android_16k_probe_validation.sh PROBE [OUTPUT_DIR]

Runs the standalone Android address-space probe against one adb target.
The target must report PAGE_SIZE=16384 and either x86_64 or aarch64.
This does not validate liba32android/Dynarmic runtime execution.
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 2
fi

probe=$1
output_dir=${2:-android-16k-probe-validation}
adb_bin=${ADB:-adb}
remote_dir=/data/local/tmp/liba32android-16k-probe

if [[ ! -f "$probe" ]]; then
    echo "missing probe: $probe" >&2
    exit 2
fi

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

mkdir -p "$output_dir"
adb_cmd get-state >/dev/null

page_size=$(adb_cmd shell getconf PAGE_SIZE | tr -d '\r\n')
if [[ "$page_size" != "16384" ]]; then
    echo "expected 16384-byte Android page size, got: $page_size" >&2
    exit 1
fi

arch=$(adb_cmd shell uname -m | tr -d '\r\n')
case "$arch" in
    x86_64|aarch64) ;;
    *)
        echo "expected x86_64 or aarch64 Android target, got: $arch" >&2
        exit 1
        ;;
esac

{
    echo "page_size=$page_size"
    echo "arch=$arch"
    echo "android.runtime_sdk=$(adb_cmd shell getprop ro.build.version.sdk | tr -d '\r\n')"
    echo "android.release=$(adb_cmd shell getprop ro.build.version.release | tr -d '\r\n')"
    echo "kernel.release=$(adb_cmd shell uname -r | tr -d '\r\n')"
    echo "kernel.version=$(adb_cmd shell uname -v | tr -d '\r\n')"
} | tee "$output_dir/environment.log"

adb_cmd shell "mkdir -p '$remote_dir'"
adb_cmd push "$probe" "$remote_dir/android_address_space_probe" >/dev/null
adb_cmd shell "chmod 755 '$remote_dir/android_address_space_probe'"

adb_cmd shell "$remote_dir/android_address_space_probe --no-log-file" |
    tr -d '\r' | tee "$output_dir/probe.log"

adb_cmd shell "$remote_dir/android_address_space_probe --execute-generated-code --no-log-file" |
    tr -d '\r' | tee "$output_dir/probe-generated-code.log"

grep -F "arch=$arch" "$output_dir/probe.log"
grep -F 'page_size=16384' "$output_dir/probe.log"
grep -F 'fastmem_4g.status=reserved' "$output_dir/probe.log"
grep -F 'fastmem_4g.commit_page=success' "$output_dir/probe.log"
grep -F 'jit_wx.mprotect_rw_to_rx=success' "$output_dir/probe-generated-code.log"
grep -F 'jit_wx.execute=RUN' "$output_dir/probe-generated-code.log"
grep -F 'jit_wx.result=42' "$output_dir/probe-generated-code.log"
grep -F 'jit_wx.result_check=PASS' "$output_dir/probe-generated-code.log"
grep -F 'probe.complete=true' "$output_dir/probe-generated-code.log"

echo "android_16k_probe_validation.status=PASS"
echo "android_16k_probe_validation.arch=$arch"
echo "android_16k_probe_validation.output_dir=$output_dir"
