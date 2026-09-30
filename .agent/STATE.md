# Current State

Last updated: 2026-09-30
Integration branch: `main`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`

Active acceptance gate:

- `post-roadmap-a32-jni-static-method-id` — ACTIVE; add the supplied VLC
  ARMv7 JNI_OnLoad-evidenced GetStaticMethodID slot and a distinct bounded
  StaticMethod logical member kind.

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

The seeded GetArrayLength slice is DONE at
`db558233a50eb79c21e65792dea4a4b74bd72d89`. The supplied ARMv7 library
proved slot 171 / offset `0x2ac`; the caller-seeded logical array registry and
real pinned-NDK ARM32 fixture both observed the exact length 7. All 11
exact-head checks passed.

The bounded static-int field slice is DONE at
`2aeb574f67dcd2b01e6593ed9947f50b528b9a76`. Supplied `libmla.so`
proved GetStaticIntField slot 150 / offset `0x258`; the runtime stores only
caller-seeded signed 32-bit values against existing StaticField member IDs, and
all 11 exact-head checks passed.

The bounded modified-UTF-8 string slice is DONE at
`d06d2ec0393c0cb12fb414d07d618b5bf1c7f07d`. NewStringUTF,
GetStringUTFChars, and ReleaseStringUTFChars are implemented at observed slots
167/169/170 with synthetic logical jstring identities, bounded owned bytes, and
one exact guest scratch lease. All 11 exact-head checks passed.

The bounded jlong-array slice is DONE at
`94dd3ed5155654956decce93dd6cbe73c25d4cf0`. NewLongArray,
GetLongArrayElements, ReleaseLongArrayElements, and SetLongArrayRegion are
implemented at observed slots 180/188/196/212 with owned int64 storage, exact
ARM32 stack decoding for the fifth SetLongArrayRegion argument, generic
GetArrayLength metadata, reference cleanup, and one bounded guest copy lease.
All 11 exact-head checks passed.

The bounded object-array slice is DONE at
`42d542ab8a14ae11ff534c6f7734f972280741c1`. NewObjectArray,
GetObjectArrayElement, and SetObjectArrayElement are implemented at observed
slots 172/173/174 with bounded owned logical jobject vectors, generic
GetArrayLength integration, local-reference return semantics, and zero leaked
array references in the real ARM32 fixture. All 11 exact-head checks passed.

The bounded instance-long-field slice is DONE at
`114d9d104ee82d1e30aa78fa785c8b389e8630db`. GetLongField and
SetLongField are implemented at observed slots 101/110 with exact ARM32 r0/r1
jlong return bits, aligned guest `[sp]`/`[sp+4]` SetLongField decoding, and
bounded values keyed by logical jobject + existing InstanceField ID. All 11
exact-head checks passed.

The bounded ThrowNew slice is DONE at
`361b9ffb044d5ed4a6cdfa080f3e93bec7893c9d`. Supplied `libmla.so` proved JNIEnv slot 14 / offset `0x38`;
the runtime keeps one bounded logical pending exception as registered class
identity plus owned message bytes, and all 11 exact-head checks passed.

The bounded CallVoidMethodV slice is DONE at
`8a528b9402a874e8d1520687dc5920248234af7b`. Supplied VLC ARMv7
`libmla.so` proved JNIEnv slot 62 / offset `0xf8`; bounded descriptor and
AAPCS32 va_list decoding normalize primitive/reference values into the
caller-owned method-call bridge, and all 11 exact-head checks passed.

The bounded byte-array element-lease slice is DONE at
`4e8326b03a8f9180700e9715126b05081ddee7b9`. Supplied ARMv7 `libfmod.so` proved
GetByteArrayElements slot 184 / `0x2e0` and ReleaseByteArrayElements slot
192 / `0x300`; caller-seeded logical jbyteArray bytes use generic length and
reference metadata plus one bounded guest copy lease with JNI_COMMIT/JNI_ABORT
semantics. All 11 exact-head checks passed.

The bounded raw CallVoidMethod slice is DONE at
`1383d7cd44b3b0a669e9e2a3e6fd7915d747efcc`. Supplied VLC ARMv7
`libvlcjni.so` proved JNIEnv slot 61 / offset `0xf4`; the runtime decodes
the first promoted 32-bit argument from r3, continues on the guest stack,
aligns 64-bit/default-promoted floating values per AAPCS32, and reuses the
accepted logical-value method-call bridge. All 11 exact-head checks passed.

The bounded instance-int-field slice is DONE at
`1408748de1a226ee3e0dce544e52237d8765530e`. Supplied VLC ARMv7 `libvlcjni.so` proved JNIEnv slot 100 /
`0x190` (`GetIntField`); caller-seeded signed 32-bit values are keyed by a
live logical jobject plus an existing InstanceField ID and return exact jint
bits in r0. All 11 exact-head checks passed.

The same supplied function also proves future ExceptionOccurred at slot 15 /
`0x3c` and ExceptionClear at slot 17 / `0x44`; those remain a separate
bounded state-model slice.

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

The bounded exception observation/clear slice is DONE at
`8490e1b059792c5cbaa34def21c13356536d9db7`. Supplied VLC ARMv7 `libvlcjni.so` proved
ExceptionOccurred slot 15 / `0x3c` and ExceptionClear slot 17 / `0x44`.
ThrowNew now reserves a bounded logical pending-exception identity;
ExceptionOccurred creates a local reference to that exact identity without
clearing pending state, and ExceptionClear preserves already returned
references while reclaiming an unobserved zero-reference pending identity.
All 11 required exact-head checks passed.

The bounded NewObjectV slice is DONE at
`eb676dda5eac4682d3ce29c488df63ef0be3ed04`. Supplied ARMv7
`libmla.so` proved JNIEnv slot 29 / `0x74`; the existing bounded
descriptor/`va_list` decoder feeds an embedding-owned constructor boundary
which may return one fresh logical jobject identity, accepted with exactly one
local reference. Raw NewObject slot 28 and NewObjectA slot 30 remain null.
All 11 required exact-head checks passed.
