# Tools

Tooling is separated by purpose.

## Android diagnostics

`android/` contains build targets and adb-oriented validation harnesses:

- `android_address_space_probe.cpp`
- `android_runtime_smoke.cpp`
- `run_android_16k_probe_validation.sh`
- `run_android_16k_validation.sh`

See `docs/diagnostics.md` before interpreting probe or runtime-smoke output.

## Fixture builders

`fixtures/` contains reproducible ARMv7 Android ELF builders used by CI and integration tests:

- `build_arm32_loader_fixture.sh`
- `build_arm32_jump_slot_fixture.sh`
- `build_arm32_versioned_fixture.sh`
- `build_arm32_android_log_shim_fixture.sh`\n- `build_arm32_libc_memory_string_fixture.sh`

They resolve fixture sources from the repository tree relative to the script
location. The Android-log and partial-libc shim builders consume sources under
`tests/compat/fixtures/`; the ELF-only builders consume
`tests/elf/fixtures/`.
