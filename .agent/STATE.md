# Current State

Last updated: 2026-09-28
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`

Active acceptance gates:

- `post-roadmap-a32-jni-register-natives` — ACTIVE; core implementation and focused host coverage exist, real ARM32 registration fixture integration still pending.
- `post-roadmap-oss-readiness` — ACTIVE; public repository surface is being made self-contained and validation-backed.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, ARM32 Bionic
libdl policy, bounded APK native-library acquisition, caller-supplied APK
runtime bootstrap composition, bounded APK native-library catalog discovery,
and the first ARM32 JNI VM/GetEnv/JNI_OnLoad bootstrap.

## Active JNI follow-up

The current JNI registration slice extends the guest `JNINativeInterface`
through real slot 215 and publishes the evidence-backed entries:

- `FindClass` at slot 6 / byte offset `0x18`;
- `RegisterNatives` at slot 215 / byte offset `0x35c`.

`A32JniVmService` now installs private ARM service stubs for GetEnv, FindClass,
and RegisterNatives while keeping unsupported entries null.

`A32JniClassRegistry` provides caller-owned bounded class handles and registered
native metadata. Guest class/function identities remain logical 32-bit values;
class, method-name, and signature strings are copied into owned host storage
under explicit ceilings.

FindClass performs bounded guest-string lookup. RegisterNatives parses ARM32
12-byte `JNINativeMethod` records transactionally and rejects malformed or
semantically invalid registrations without partial mutation.

`invoke_a32_registered_native_noargs` provides the first narrow reverse-dispatch
transaction for exact registered zero-Java-argument methods through the existing
bounded service-aware A32 executor.

Focused host coverage exercises table/stub bytes, class lookup, registration
validation/rollback, exact native lookup, one reverse-dispatch execution, and
the pre-existing JNI_OnLoad/GetEnv behavior.

The dedicated pinned-NDK JNI fixture still proves only JNI_OnLoad + GetEnv. The
remaining acceptance work is to extend that real ARM32 fixture to perform
FindClass + RegisterNatives and then invoke one registered guest native.

## Public repository readiness

The repository has a versioned public C embedding API, reproducible fixture
builders, architecture/development documentation, CI workflows, and a large
host regression suite.

The current OSS-readiness change adds a clearer public landing page, roadmap,
contribution/security/conduct guidance, GitHub collaboration templates, a public
API quick start, repository/docs link validation, and CI for repository hygiene.

The project license is still intentionally unresolved. A public repository is
not itself a license grant; selecting and committing the project license remains
a release/OSS-readiness blocker that requires an explicit maintainer decision.

## Deferred / partial

JNI member IDs, reference lifetime, strings/arrays/exceptions, general Java
method/field calls, thread attach/detach, JNI_OnUnload, Android framework
services, graphics/windowing, audio, input/events/sensors, automatic app
adaptation/patching, broad real-device execution, and higher-level app/runtime
orchestration remain separate work.

Also deferred are broader Bionic/pthread/TLS coverage, full Android linker/APK
search semantics, true lazy binding, caller-relative RTLD_NEXT, process-exit
Global/NODELETE teardown, concurrent graph mutation, `DT_PREINIT_ARRAY`, and
general process argv/envp constructor ABI.
