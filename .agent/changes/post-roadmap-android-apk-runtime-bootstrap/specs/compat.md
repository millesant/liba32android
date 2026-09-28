# Compatibility spec delta — caller-supplied APK runtime bootstrap

`A32AndroidApkRuntimeBootstrap` is a persistent composition object above
accepted APK/search/provider/link-map/libdl primitives. It is not a second ELF
loader.

The caller supplies:

- one exact APK filesystem path;
- one relative ABI directory such as `lib/armeabi-v7a`;
- one finite complete set of application-local bare SONAMEs;
- one existing platform dependency provider;
- caller-owned mapped memory, persistent link map, synthetic handle table, and
  lifecycle state; and
- the existing bounded APK/search/open-transaction options.

Application SONAME count and bytes are caller bounded. Names must be non-empty,
NUL-free, slash-free, backslash-free, and unique. The ABI directory is bounded,
relative, NUL-free, backslash/! free, and contains no empty, dot, or dot-dot
components. Every exact application identity is constructed as
`<apk>!/<abi-dir>/<soname>` under both source and search path ceilings.

The bootstrap owns the APK source, exact requester-root records, one
requester-scoped Android application provider, one APK-local guard/root
provider, the ordered provider chain, and one `A32LibDlOpenTransaction`.

Provider order is:

1. requester-scoped application search;
2. APK-local guard/root provider;
3. caller-owned platform provider.

Context-free application search returns NotFound, so a declared application
SONAME is opened by the APK root provider. For requester-aware dependency
lookup, the exact requester-scoped provider resolves APK-local edges first. If a
declared application SONAME is absent from the APK, the guard returns Failed and
prevents platform substitution. Undeclared names may fall through to the
platform provider.

`open_root` accepts only a declared application SONAME and delegates directly
to `A32LibDlOpenTransaction::open`. Dependency append, eager relocation, GNU
RELRO sealing, dependency-first constructors, synthetic handle publication,
load-policy promotion, cleanup, and failure retention therefore keep their
accepted semantics.

The bootstrap must outlive any libdl service/open transaction that borrows its
provider composition. It exposes the same persistent open transaction for later
app-local dlopen.

Automatic package discovery, manifest parsing, ABI selection, split APKs,
AssetManager/package-manager integration, JNI startup, graphics/audio,
automatic patching, and device deployment remain separate.
