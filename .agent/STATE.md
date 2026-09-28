# Current State

Last updated: 2026-09-27
Integration branch: `bleeding`
Control-plane round: `millesant/.gpt@f4e926e81ad91d13d02a006f4a18a00f66ae0bab`
Active acceptance gate: `post-roadmap-android-apk-runtime-bootstrap` — IMPLEMENTED, exact-head validation NOT RUN.

## Phase

Features 011-049 remain accepted and the numbered roadmap remains COMPLETE.

Accepted post-roadmap work includes service-aware ELF lifecycle,
`__aeabi_atexit` / `__cxa_finalize`, dynamic missing-object `dlopen`,
ownership/reclamation planning, physical reclamation, targeted final-close
`dlclose` unload, lifecycle-scoped automatic DSO association, ARM32 bionic
libdl load-policy semantics, and bounded APK native-library acquisition.

Bounded APK native-library acquisition is DONE at
`4c3cca6e8bd9b04c4995e82b83c5976f9be52763`. The project operator confirmed
all required exact-head CI checks were green.

## Active post-roadmap follow-up

Caller-supplied APK runtime bootstrap composition is implemented.

`A32AndroidApkRuntimeBootstrap` owns bounded application SONAME/identity
storage, the accepted APK source, exact requester roots, requester-scoped
application search, an APK-local guard/root provider, the ordered provider
chain, and one existing `A32LibDlOpenTransaction`.

The caller supplies the exact APK path, a relative ABI directory such as
`lib/armeabi-v7a`, one finite complete application-local SONAME set, an
existing platform provider, persistent mapped-memory/link-map/handle/lifecycle
state, and all existing source/search/open resource bounds.

Application SONAMEs are bounded, bare, unique names. ABI directory components
are bounded and reject NUL, backslash, `!`, absolute paths, empty components,
dot, and dot-dot. Exact app identities are
`<apk>!/<abi>/<soname>` under the existing path ceilings.

Provider order is requester-scoped app search -> APK-local guard/root ->
platform provider. Context-free bootstrap/later app-root opens are resolved by
the guard/root provider. Requester-aware app dependencies use exact requester
roots first. A declared app-local SONAME missing from the APK fails closed and
cannot silently fall through to a same-named platform library; undeclared names
still fall through to platform policy.

`open_root` only validates declared membership and delegates to
`A32LibDlOpenTransaction`; persistent append, eager relocation, GNU RELRO,
dependency-first constructors, synthetic handle publication, load policy, and
failure cleanup therefore keep the accepted implementations.

Focused host regressions cover exact identities, provider ordering, declared
context-free app roots, transitive requester search, missing-declared fail
closed, undeclared platform fallback, duplicate/count/path/ABI validation, and
invalid root rejection.

The real pinned-NDK ARM32 integration now packages both root and child DSOs into
one deterministic DEFLATED mini-APK, starts from an empty link map, bootstraps
the root from the APK, resolves/initializes the child transitively, executes the
relocated root call, and then acquires the resident child through the same
persistent open transaction.

Exact-head validation: the first attempt at `b72ef8e6234288d6fdd75ed0c52362d4cdba9aea` had one operator-reported CI failure. Static audit found and corrected an impossible child-handle expectation: slot 1 is `0x70000004`, not `0x70000001`. Corrected exact-head validation is pending.

## Deferred / partial

Automatic APK ABI native-library catalog discovery, package-manager/AssetManager
discovery, manifest/root selection, ABI auto-selection, split-APK selection,
APK signature verification, ZIP64/encrypted archive support, explicit-path
dlopen, true lazy binding, caller-relative RTLD_NEXT, process-exit
Global/NODELETE teardown, DF_1_GLOBAL-specific unload retention beyond explicit
libdl root policy, Retired-slot compaction, concurrent graph mutation,
`DT_PREINIT_ARRAY`, process argv/envp constructor ABI, broader pthread/TLS,
higher-level public ELF/platform orchestration, JNI/graphics/audio surfaces,
automatic app patching, and real Android device execution remain separate.
