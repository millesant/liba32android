# Current State

Last updated: 2026-09-28
Integration branch: `main`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`

Active acceptance gate:

- `post-roadmap-a32-jni-array-length` — ACTIVE; add the supplied ARMv7-evidenced
  GetArrayLength slot with bounded caller-seeded APK-agnostic array metadata.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, ARM32 Bionic
libdl policy, bounded APK native-library acquisition, caller-supplied APK
runtime bootstrap composition, bounded APK native-library catalog discovery,
the ARM32 JNI VM/GetEnv/JNI_OnLoad bootstrap, and bounded JNI
FindClass/RegisterNatives plus zero-Java-argument registered-native reverse
dispatch.

The JNI VM/GetEnv/JNI_OnLoad bootstrap is DONE with exact-head evidence at
`d8564a7a465f1099e8c8b415997f0d1812e2998e`. The pinned-NDK integration and focused bootstrap regressions are
green there.

`post-roadmap-oss-readiness` is also DONE with exact-head evidence at
`d8564a7a465f1099e8c8b415997f0d1812e2998e`. All nine workflows triggered for that head concluded success,
including repository hygiene, the public C API, compatibility integrations,
and the main CI workflow.

## Accepted JNI registration slice

The current JNI registration slice extends the guest `JNINativeInterface`
through real slot 215 and publishes the evidence-backed entries:

- `FindClass` at slot 6 / byte offset `0x18`;
- `RegisterNatives` at slot 215 / byte offset `0x35c`.

`A32JniVmService` installs private ARM service stubs for GetEnv, FindClass, and
RegisterNatives while keeping unsupported entries null.

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

The pinned-NDK ARM32 fixture now executes GetEnv, FindClass, and
RegisterNatives from JNI_OnLoad, preserves the exact
`org/videolan/Fixture / nativePing / ()I` guest binding, and then reverse
dispatches that registered native with return value 42. Exact-head validation
at `dbbcb06d5a9e0225b9e0a9c3515b646c0fff6503` passed all 11 required
check-runs, including ARM32 JNI registration integration check
`109225192284`.

The bounded member-ID slice is DONE at
`f073092cf631a98e274bce9cdb54d0f73ad7099d`. Supplied `libmla.so`
machine code proved GetMethodID slot 33, GetFieldID slot 94, and
GetStaticFieldID slot 144; the implementation publishes those exact entries and
uses caller-seeded logical member handles with exact class/kind/name/signature
lookup. All 11 exact-head checks passed.

The bounded JavaVM thread slice is DONE at
`35168f13294de7f88ed7b7054f0b08f1c9f5e7e9`. The supplied ARMv7 library
proved AttachCurrentThread slot 4 / offset `0x10` and DetachCurrentThread slot
5 / offset `0x14`; the implementation models one attached/detached guest JNI
context, returns JNI_EDETACHED from GetEnv while detached, and passed all 11
exact-head checks.

The bounded strong/local reference slice is DONE at
`764658ec7a6bde80b2cc6b0474bba75f0dd79d1b`. The supplied ARMv7 library
proved NewGlobalRef slot 21 / `0x54`, DeleteGlobalRef slot 22 / `0x58`, and
DeleteLocalRef slot 23 / `0x5c`; the implementation keeps APK-agnostic logical
local/global counts and the real ARM32 fixture ends with zero live class
references. All 11 exact-head checks passed.

The active array slice is deliberately small: the same supplied `libmla.so`
loads GetArrayLength from JNIEnv slot 171 / offset `0x2ac`. The runtime seeds
finite logical array metadata from the caller; allocation and elements remain
separate work.

## Public repository readiness

The public surface now includes a clear maturity/scope-oriented landing page,
roadmap, contribution/security/conduct guidance, CODEOWNERS, issue/PR templates,
a public C API quick start, repository/documentation hygiene validation, and CI
for that hygiene contract.

The GitHub repository description and topics now reflect the actual AArch32,
Android, ELF, JNI, compatibility-layer, and binary-translation scope. The unused
wiki is disabled so maintained documentation remains in Git.

The public landing page and documentation index now expose English and
Português (Brasil) entry points. The repository's integration/default branch is
`main`; current badges, security guidance, project/agent metadata, and all nine
workflow branch filters were reconciled to that rename. Exact-head validation at
`ba778a9bb81c3776f568f5f67a9a7e863ef204fb` reported all 11 check-runs
successful, including repository/docs validation.

The stale pre-v7 root `specs/` tree was retired from the working tree at
`9c63837aab2f0c1e1fe869c3eac9bae2a98dfcff`. It contained 45 Markdown files
across feature packages 000-014; the exact historical tree remains recoverable
from `56a438e426408465ec23bac6c09960a792be090b`, while `.agent/specs/` remains
the sole accepted current specification surface.

Local exact-head validation at `d8564a7a465f1099e8c8b415997f0d1812e2998e` built successfully, passed the
locally configured 61-test suite including the pinned-NDK JNI integration, and
passed the external C11 public-API consumer. GitHub Actions then reported all
nine triggered workflows successful.

The maintainer selected Apache License 2.0 for liba32android. The canonical
license text is committed at the repository root, public contribution guidance
uses the same inbound terms by default, and project-license selection is no
longer an OSS-release blocker. GitHub recognizes Apache-2.0 and all 11 exact-head
check-runs passed at 0e1a0b76f8568e53e9843e58b8f12768276f4c4c.

## Deferred / partial

JNI reference lifetime, strings/arrays/exceptions, general Java
method/field calls, broader thread/VM semantics, JNI_OnUnload, Android framework
services, graphics/windowing, audio, input/events/sensors, automatic app
adaptation/patching, broad real-device execution, and higher-level app/runtime
orchestration remain separate work.

Also deferred are broader Bionic/pthread/TLS coverage, full Android linker/APK
search semantics, true lazy binding, caller-relative RTLD_NEXT, process-exit
Global/NODELETE teardown, concurrent graph mutation, `DT_PREINIT_ARRAY`, and
general process argv/envp constructor ABI.
