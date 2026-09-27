# Diagnostics and error sharing

Status: current CI, address-space probe, Android runtime-smoke, internal runtime-service behavior, and feature-049 public embedding/error API implementation documented; exact-head feature-049 validation pending.

## Goal

When something fails on a device, emulator, CI runner, or future embedding application, the useful evidence should be copy-pasteable into an issue or chat without requiring interpretation first.

Do not replace the original failure text with a paraphrase. Keep addresses, errno values, PCs, symbols and component names intact.

## Current GitHub Actions failures

For GitHub CI, the preferred report is simply the Actions run URL. The repository workflow keeps named steps and uses `ctest --output-on-failure`, so the run URL is enough for a maintainer/agent with repository access to inspect the exact failure.

If sharing text instead of a URL, copy the failed step name, first explicit error/fatal line, nearby context, and final command exit status when shown.

## Android runtime smoke (preferred next device test)

The `android-runtime-smoke-<sha>` CI artifact contains:

```text
android_runtime_smoke
liba32android.so
run.sh
README.txt
```

The executable is linked to the real `liba32android.so`. `run.sh` resolves its own directory, prepends that directory to `LD_LIBRARY_PATH`, and then starts the smoke. This avoids requiring a globally installed shared library or relying on an ELF `$ORIGIN` RUNPATH that the Android toolchain may omit.

Do **not** execute the bundle directly from Android shared storage such as `~/storage/downloads`: that filesystem is normally `noexec`. Copy/extract the files into a Termux-private executable directory first. Example:

```sh
mkdir -p ~/liba32-smoke
cp ~/storage/downloads/android_runtime_smoke ~/liba32-smoke/
cp ~/storage/downloads/liba32android.so ~/liba32-smoke/
cp ~/storage/downloads/run.sh ~/liba32-smoke/
cd ~/liba32-smoke
chmod 700 android_runtime_smoke run.sh
./run.sh
```

If the artifact was extracted into a subdirectory, copy that directory's files instead of the exact example paths above.

The normal smoke performs:

- creation of the mapped 4 GiB guest address space;
- A32 `mov r0,#42` through `liba32android` / Dynarmic;
- mapped A32 `STR` + `LDR` through the fastmem-capable backend;
- verification that the mapped load/store used direct fastmem rather than data callbacks.

Important output includes:

```text
runtime_smoke.version=1
android.ndk_api=26
android.runtime_sdk=...
android.release=...
fastmem.available=true
fastmem.base=0x...
a32.return42.fastmem_enabled=true
a32.return42.r0=42
a32.return42.status=PASS
a32.memory.fastmem_enabled=true
a32.memory.data_read_callbacks=0
a32.memory.data_write_callbacks=0
a32.memory.fastmem_direct=true
a32.memory.status=PASS
runtime_smoke.complete=true
```

`android.ndk_api` is the build target. `android.runtime_sdk` and `android.release` are read from the actual Android system at runtime and must be used when identifying the device OS.

After the normal smoke passes, the optional fastmem fault/fallback validation is:

```sh
./run.sh --exercise-fastmem-fault
```

Its success shape includes:

```text
a32.fastmem_fault.memory_fault=true
a32.fastmem_fault.data_read_callbacks=<nonzero>
a32.fastmem_fault.status=PASS
```

This intentionally accesses an unmapped **guest** page. Dynarmic should recover the host fastmem fault and route the guest access to the callback path; the process itself should not crash.

## Runtime-smoke file logging

The runtime smoke mirrors stdout/stderr into a file. Selection order is:

1. `--log-file PATH` or `LIBA32ANDROID_LOG_FILE`;
2. `$HOME/storage/downloads/liba32android-runtime-smoke.log` (Termux convenience path);
3. `/sdcard/Download/liba32android/runtime-smoke.log`;
4. `$HOME/liba32android/runtime-smoke.log`;
5. `./liba32android-runtime-smoke.log`.

The selected path is emitted as:

```text
diagnostics.log_file=<path>
```

For the user's known Termux setup, the expected convenient result is normally:

```text
/data/data/com.termux/files/home/storage/downloads/liba32android-runtime-smoke.log
```

which is visible through Android Downloads because `~/storage/downloads` is Termux's storage link.

## Runtime-smoke errors and crashes

The smoke already emits scoped structured failures such as:

```text
A32ERR|component=android_runtime_smoke|code=MAP_RETURN42|message=failed to prepare code page
A32ERR|component=android_runtime_smoke|code=EXCEPTION|message=...
```

It also installs minimal async-signal-safe markers for `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL`, and `SIGSEGV`:

```text
A32CRASH|component=android_runtime_smoke|signal=11|pid=12345|addr=0xdeadbeef
```

Dynarmic itself owns the active POSIX `SIGSEGV` handler while a JIT exists so it can recover fastmem page faults. Recoverable faults whose PC belongs to Dynarmic-generated code are consumed by Dynarmic and do not reach the smoke crash marker. For a fault Dynarmic does not recognize, its handler chains to the previously installed smoke handler by calling that function directly. The smoke therefore restores `SIG_DFL` explicitly after writing `A32CRASH`; relying only on `SA_RESETHAND` would be insufficient in that direct-call chaining path.

If the runtime smoke crashes, send the complete log plus that marker and any Android tombstone/native backtrace. The crash marker supplements Android's normal crash handling; it does not replace a tombstone.

Fatal-diagnostic validation is now an explicit destructive mode:

```sh
./run.sh --crash-test
```

The mode is mutually exclusive with `--exercise-fastmem-fault`. It installs and verifies the crash-marker handlers, prints `crash_test.status=ARMED` and `crash_test.signal=SIGABRT`, then calls `abort()`. It is never exercised by normal smoke execution or by CI. On 2026-09-19, the CI #134 artifact was run in Termux after a successful normal smoke: the crash invocation emitted the armed/SIGABRT lines plus `A32CRASH|component=android_runtime_smoke|signal=6|...`, and the shell reported `Aborted`. An Android tombstone/native backtrace was not captured, so tombstone coexistence remains **NOT OBSERVED**.

## Android address-space probe

`android_address_space_probe` remains the lower-level mapping/JIT diagnostic. It prints machine-readable `key=value` lines and mirrors them to a file when available.

The standalone probe now matches `android_runtime_smoke`'s environment naming: `android.ndk_api` reports the compile-time NDK/API target (`__ANDROID_API__`), while `android.runtime_sdk` and `android.release` come from the device's `ro.build.version.sdk` and `ro.build.version.release` system properties. Existing historical probe logs that contain `android.api=26` should still be interpreted as compile-time target metadata, not runtime SDK evidence.

The address-space probe's automatic file selection is:

1. `/sdcard/Download/liba32android/address-space-probe.log`;
2. `$HOME/liba32android/address-space-probe.log` when `HOME` is defined;
3. `./liba32android-address-space-probe.log`.

It can be overridden with `--log-file PATH` or `LIBA32ANDROID_LOG_FILE`; `--no-log-file` disables file output explicitly.

The exact selected location is always printed through `diagnostics.log_file=`.

The standalone probe installs the analogous marker:

```text
A32CRASH|component=android_address_space_probe|signal=11|pid=12345|addr=0xdeadbeef
```

Its generated-code mode is `--execute-generated-code`. The mutually exclusive `--crash-test` mode follows the same explicit pattern as runtime smoke: after successful crash-handler setup it prints the armed/SIGABRT markers and calls `abort()` without running the mapping/JIT probes. Real-device evidence for the existing probe is stored under `docs/research/evidence/`.

## PC-hosted 16 KiB Android validation

A second physical phone is not required for the page-size-specific check. Android's official guidance provides experimental 16 KiB emulator system images for Android 15 or newer; verify the running target with `adb shell getconf PAGE_SIZE`, which must return `16384`.

For project-valid runtime evidence, use the ARM64 16 KiB image and run `tools/android/run_android_16k_validation.sh` against the matching `android_address_space_probe`, `android_runtime_smoke`, and `liba32android.so` outputs. The harness rejects a non-AArch64 or non-16 KiB target and captures the normal probe, generated-code probe, runtime smoke, and fastmem-fallback outputs without invoking the destructive crash test.

Official setup reference: https://developer.android.com/guide/practices/page-sizes

## Fedora 16 KiB emulator probe

For the native Fedora 44 x86_64/KVM environment, the page-size/address-space path uses Google's experimental Android 15 x86_64 16 KiB image. Verify the running target first:

```sh
adb shell getconf PAGE_SIZE
adb shell uname -m
```

The required result is `16384` and `x86_64`. Then run the matching x86_64 `android_address_space_probe` through:

```sh
tools/android/run_android_16k_probe_validation.sh android_address_space_probe
```

This harness validates Android/kernel address-space behavior only. It does not load `liba32android.so` and must not be reported as AArch64 Dynarmic/runtime evidence.

On the observed Fedora host, Android Emulator 37.1.11 initially crashed in its software-renderer path. Starting the AVD with host OpenGL and Vulkan disabled completed boot:

```sh
emulator -avd liba32_16k \
  -no-window -no-audio -no-boot-anim -no-snapshot \
  -gpu host -feature -Vulkan
```

## Android storage boundary

An executable launched as the `adb shell` user, a Termux process, and a normal embedded application do not have identical storage access. Android scoped storage also limits direct shared-storage access for apps targeting modern Android.

Therefore the generic runtime must not assume it can always write `/sdcard/Download`. The current Termux/probe paths are diagnostic conveniences. A future host application must provide an app-appropriate writable directory or logging sink and perform user-visible export through its Android integration.

Android storage references:

- https://developer.android.com/training/data-storage
- https://developer.android.com/training/data-storage/shared/documents-files

## Runtime-wide structured error format

Feature 049 implements the first stable public C embedding/error surface.
`include/liba32android/liba32android.h` exposes versioned opaque-runtime
memory/execution operations and an optional caller-owned error buffer.

Public failures use this stable text shape:

```text
A32ERR|component=<component>|code=<stable_code>|pc=<guest_pc>|addr=<guest_addr>|message=<human text>
```

Fields that do not apply may be omitted. Intended future examples:

```text
A32ERR|component=memory|code=GUEST_MEMORY_FAULT|pc=0x00104210|addr=0xdeadbeef|message=read from unmapped guest address
A32ERR|component=elf|code=ELF_BAD_MACHINE|message=expected EM_ARM
A32ERR|component=linker|code=UNRESOLVED_SYMBOL|message=symbol foo was not found
```

The stable `code` is for diagnosis/automation; the `message` is for humans. Guest addresses and guest PC should be hexadecimal when available.

## What to send here

The best evidence, in priority order:

1. GitHub Actions run link for CI failures;
2. the complete `liba32android-runtime-smoke.log` from the Android runtime smoke;
3. the complete `address-space-probe.log` for low-level mapping research;
4. any `A32CRASH|...` marker plus Android tombstone/backtrace if the process crashed;
5. any `A32ERR|...` lines plus nearby context.

Do not summarize away guest addresses, callback counts, PCs, errno values, or the `runtime_smoke.complete` line. Those fields are often exactly what distinguishes a loader, mapping, JIT, or guest-memory failure.
