# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-a32-jni-onload-bootstrap` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, ARM32 bionic
libdl policy, bounded APK native-library acquisition, caller-supplied APK
runtime bootstrap composition, and bounded APK native-library catalog
discovery.

Bounded APK native-library catalog discovery is DONE at
`eea5433f77049ed1cccddd5998213b6816194e63`. The project operator confirmed
all required exact-head CI checks were green.

## Active post-roadmap follow-up

The first ARM32 JNI compatibility slice is implemented.

`A32JniVmService` installs caller-addressed guest JavaVM/JNIEnv objects,
pointer tables, and one ARM `svc #0xd7; bx lr` GetEnv stub transactionally.
Only logical 32-bit guest pointers are published; the caller owns mappings and
permission changes.

The JavaVM invocation table follows Android layout and publishes GetEnv at slot
6 / byte offset `0x18`. Other JavaVM invoke entries are null. The JNIEnv
native table is currently only a five-word zeroed prefix; no Java/class/native
method operations are published yet.

Matching Dalvik, GetEnv first validates the requested JNI version. The inclusive
numeric range `0x00010001..0x00010006` (JNI 1.1 through 1.6) succeeds for the
currently-attached guest context and writes the configured logical JNIEnv
pointer. An out-of-range request returns JNI_EVERSION before touching the
output slot. Exact JavaVM identity and required guest writes are validated.

`invoke_a32_jni_on_load` resolves `JNI_OnLoad` inside one exact loaded graph
object only, invokes it as `JNI_OnLoad(JavaVM*, nullptr)` through the bounded
service-aware A32 executor, and accepts only exact JNI 1.2, 1.4, or 1.6 return
versions, matching Dalvik/ART load-time behavior.

An optional borrowed lifecycle execution context is scoped to the exact
JNI_OnLoad object during the guest call and restored afterward. Nested
`__aeabi_atexit` registration therefore retains the same automatic DSO
ownership provenance as constructor execution.

Focused host regressions cover exact guest table/stub bytes, invalid/overlapping
layout, transactional install rollback, Dalvik GetEnv range/output ordering,
wrong VM/output pointers, exact-object JNI_OnLoad isolation, supported and bad
OnLoad versions, and resource option validation.

The dedicated pinned-NDK ARM32 integration builds a deterministic dependency-
free `libfixture_jni_onload.so`. Its exported JNI_OnLoad performs an indirect
JavaVM::GetEnv call through table offset `0x18`, requires a non-null JNIEnv,
and returns JNI 1.6. The harness loads the ELF, installs/seals the JNI guest
stub, proves exact lifecycle object context during the SVC, and executes the
function with one handled service call.

Supplied ARMv7 evidence:
- `libvlc.so`, `libvlcjni.so`, `libmla.so`, and `libfmod.so` export
  JNI_OnLoad;
- `libmla.so` machine code uses JavaVM offset `0x18` for GetEnv;
- its JNIEnv::FindClass wrapper uses native-table offset `0x18` (slot 6);
- its JNIEnv::RegisterNatives wrapper uses offset `0x35c` (slot 215).

Exact-head validation: NOT RUN.

## Deferred / partial

JNI FindClass/RegisterNatives/native-method dispatch, Java class/reference
state, strings/arrays/exceptions, thread attach/detach, JNI_OnUnload, Android
framework services, graphics/windowing, audio, input/events/sensors, automatic
app adaptation/patching, real device execution, and higher-level app/runtime
orchestration remain substantial separate work.

Also deferred: ABI auto-detection, manifest/root-library selection, split-APK
merging, package-manager/AssetManager discovery, APK signatures,
ZIP64/encrypted archives, explicit-path dlopen, true lazy binding,
caller-relative RTLD_NEXT, process-exit Global/NODELETE teardown,
DF_1_GLOBAL retention outside explicit libdl policy, Retired-slot compaction,
concurrent graph mutation, `DT_PREINIT_ARRAY`, process argv/envp constructor
ABI, and broader pthread/TLS.
