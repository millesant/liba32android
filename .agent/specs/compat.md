# Compatibility contract

Status: Accepted current project contract
Last reconciled: 2026-09-26

## L32-C001 — Platform compatibility is a separate layer

Platform-library behavior belongs under `src/compat/`, above the
game-agnostic runtime service-dispatch/registry contracts. Compatibility code
may use logical guest addresses and `memory::GuestMemory` but may not expose
host pointers as guest pointers or leak Dynarmic types.

## L32-C002 — Bounded A32 `__android_log_write` service bridge

The Android log-write compatibility service models the C function
`int __android_log_write(int prio, const char* tag, const char* text)` through
the base AAPCS32 register convention: guest r0 carries the 32-bit priority, r1
the tag pointer, r2 the text pointer, and the signed 32-bit return value is
written back to r0.

The service is configured with one exact caller-selected SVC immediate and a
caller-owned sink. Other SVC immediates remain `Unhandled`. Guest strings are
copied only through `GuestMemory` under separate caller-provided maximum tag
and text payload lengths. A tag pointer of zero is preserved as a null tag for
the sink. A null text pointer, unreadable string, address overflow, or missing
terminator within its payload ceiling returns `Failed` before sink invocation.

The sink receives byte-preserving bounded string views valid only for the
synchronous call and returns a signed 32-bit result whose bit pattern is copied
to guest r0. The service does not validate Android logging policy itself and
does not call host liblog directly.

## L32-C003 — Deliberate log-service exclusions

Feature 026 does not implement `__android_log_print`,
`__android_log_vprint`, varargs/stack marshalling, Android log filtering,
guest ELF symbol export/provider generation, namespace/search policy, JNI,
graphics, audio, or general libc emulation.

## L32-C004 — ARM32 guest `liblog.so` write shim/provider

The compatibility layer defines one private guest/host SVC protocol for its
first log shim. `LIBA32ANDROID_A32_ANDROID_LOG_WRITE_SHIM_SVC` and
`kA32AndroidLogWriteShimSvcImmediate` both represent immediate `0xA0`.
Guest shim source and host integration code must consume that shared definition
rather than independently choosing service numbers.

The reproducible freestanding ARM32 shim has SONAME `liblog.so`, exports
`__android_log_write` as a function, preserves its incoming AAPCS32
registers, executes exactly the private SVC, and returns with `bx lr`.
A companion real ARM32 consumer may depend on `liblog.so` and resolve that
symbol through ordinary eager `R_ARM_JUMP_SLOT` handling.

`make_a32_android_log_shim_catalog_entry` returns an exact-name
`Elf32DependencyCatalogEntry` using SONAME `liblog.so` and stable opaque
identity `liba32android-compat-liblog`. The returned entry borrows the supplied
image bytes; their backing storage must outlive the provider operation.

The runtime does not embed or own the generated shim DSO. Automatic platform
catalog installation, filesystem/namespace search policy,
`__android_log_print`/`__android_log_vprint`, and general `liblog.so`
compatibility remain separate work.

## L32-C005 — Explicit Android platform-provider policy seam

`A32AndroidPlatformProvider` is a requester-aware
`Elf32DependencyProvider` for concrete compatibility libraries. Feature 028
recognizes only the exact name `liblog.so`; every other requested name returns
`NotFound` without invoking platform-access policy.

For an exact `liblog.so` request, the provider passes the borrowed requester
identity and requested name byte-for-byte to a caller-owned
`A32AndroidPlatformAccessPolicy`. `Allow` delegates to the existing exact
catalog semantics, including non-empty image and maximum-image-byte validation.
`NotFound` and `Failed` are propagated without publishing source data.
Context-free `resolve` is represented to policy by an empty requester
identity.

The provider borrows both the policy object and generated liblog image for its
entire lifetime. It is non-copyable and non-movable because its internal catalog
span refers to its own entry storage. It does not normalize request names,
interpret pathnames, define Android namespaces/links, inspect filesystem/APK
paths, or install itself automatically into an application provider chain.

## L32-C006 — Direct Android namespace SONAME accessibility

`A32AndroidNamespaceAccessPolicy` implements
`A32AndroidPlatformAccessPolicy` using caller-owned finite requester bindings
and direct namespace links.

A requester is bound by exact opaque identity bytes to exactly one non-empty
namespace. Missing bindings return `NotFound`; duplicate matching bindings or
an empty matching namespace return `Failed`. If the requester namespace equals
the configured non-empty platform namespace, the request is `Allow`.

Otherwise exactly one direct link from requester namespace to platform
namespace is required. No matching link returns `NotFound`; duplicate matching
links return `Failed`. A matching link chooses exactly one accessibility mode:
`allow_all_shared_libs=true` with no explicit names, or a non-empty finite
list of non-empty exact SONAME bytes. Combining both modes or providing neither
returns `Failed`. Explicit mode returns `Allow` only on an exact requested
name match; otherwise `NotFound`.

The policy borrows all bindings, links, nested SONAME spans, and string storage
for its lifetime and performs no allocation. It does not infer namespaces from
paths, traverse namespace links transitively, search files/APKs, parse Android
linker configuration, model RUNPATH/RPATH/LD_LIBRARY_PATH/preload/RTLD policy,
or automatically install platform providers.


## L32-C007 — Bounded A32 libc memory/string host service

The compatibility layer may expose one bounded host-service handler for the
seven concrete libc primitives observed in both supplied ARM32 targets:
`memcpy`, `memset`, `memcmp`, `memchr`, `strlen`, `strcmp`, and
`strncmp`.

The shared guest/host private service IDs are `0xA1` through `0xA7` in that
order. The service consumes their ordinary AAPCS32 word arguments from r0-r2
and writes the result to r0. Pointer results are logical 32-bit guest addresses;
comparison results preserve only the required negative/zero/positive contract.

The caller supplies finite `max_transfer_bytes` and `max_string_bytes`
ceilings. Transfer counts above the first ceiling, string/strncmp scans beyond
the second, logical address wrap, or guest-memory access failure return
`Failed`. Unknown service IDs remain `Unhandled`.

Zero-count memory/strncmp calls access no guest memory. `strlen` accepts a
payload exactly at the string ceiling when followed by NUL. `strcmp` may
return once a differing byte determines the result; otherwise an equal
unterminated pair at the ceiling fails. `strncmp` reads at most its explicit
count and need not observe a terminator.

Feature 030 adds no guest `libc.so`, allocator, stdio/file/socket/pthread/dl
state, errno model, libm behavior, or platform-provider installation.


## L32-C008 — Finite requester-aware Android platform catalog

`A32AndroidPlatformCatalogProvider` may expose a caller-owned finite span of
exact `Elf32DependencyCatalogEntry` records through the existing
requester-aware platform-access-policy seam.

A request with no exact catalog name returns `NotFound` without invoking
policy. Duplicate exact requested names return `Failed` before policy.
Exactly one name match is forwarded byte-for-byte with requester identity to
`A32AndroidPlatformAccessPolicy`. `Allow` delegates to the existing exact
catalog provider, preserving its identity/image/maximum-byte validation;
`NotFound` and `Failed` propagate without source data.

The provider owns no entry, string, identity, image, or policy storage. All
borrowed storage must outlive it. The feature-028 one-slot
`A32AndroidPlatformProvider` remains valid as a `liblog.so` convenience
provider.

The finite catalog adds no automatic catalog construction, path/search logic,
namespace inference, guest `libc.so` binary, or broader platform behavior.


## L32-C009 — Partial ARM32 libc memory/string shim/provider path

The compatibility layer may build a reproducible freestanding ARM32 DSO with
SONAME `libc.so` that exports only the seven feature-030 primitives. Each
export preserves incoming AAPCS32 argument registers, executes its shared
private SVC ID `0xA1` through `0xA7`, and returns with `bx lr`.

`make_a32_libc_memory_string_shim_catalog_entry` returns a borrowed exact-name
catalog entry for `libc.so` with stable opaque identity
`liba32android-compat-libc-memory-string`.

A real freestanding ARM32 consumer may depend on that shim through ordinary
`DT_NEEDED libc.so` plus eager JUMP_SLOT relocations. Integration may acquire
the shim through feature-031 finite platform catalog plus feature-029 namespace
policy and execute every wrapper through feature-030 services.

The partial shim is not Android libc and does not claim sufficiency for supplied
FMOD/VLC targets. Allocation, pthreads, I/O/stdio/socket, errno, dynamic-loader,
math, process-startup, signal, locale, and all other libc behavior remain out of
scope.


## L32-C010 — Additional bounded libc copy/search services

The existing `A32LibcMemoryStringService` may additionally recognize
`memmem`, `strcpy`, and `strncpy` through shared private SVC IDs
`0xA8`, `0xA9`, and `0xAA`.

`memmem` consumes haystack pointer/length in r0/r1 and needle pointer/length
in r2/r3. Both explicit lengths are bounded by `max_transfer_bytes`; logical
range wrap or GuestMemory read failure returns `Failed`. An empty needle
returns the haystack pointer without guest reads; a haystack shorter than a
non-empty needle returns null without guest reads; otherwise the first exact
byte match returns a logical guest pointer.

`strcpy` reads the complete source including NUL into temporary storage before
destination mutation. The source payload must terminate within
`max_string_bytes`, the full copy including NUL must fit
`max_transfer_bytes`, and the destination logical range must not wrap.

`strncpy` bounds its explicit count by `max_transfer_bytes`. Zero count
performs no guest access. Non-zero calls read at most count source bytes, stop
reading after NUL and pad the temporary destination with NULs, or copy exactly
count bytes without inventing a terminator when no NUL is encountered.

Feature 033 adds no integer parsing, errno, allocation, thread, I/O,
dynamic-loader, or guest-shim export behavior.


## L32-C011 — Partial libc copy/search shim extension

The prepared partial ARM32 `libc.so` may additionally export `memmem`,
`strcpy`, and `strncpy` using the shared feature-033 SVC IDs `0xA8`,
`0xA9`, and `0xAA`, with each guest function consisting only of its SVC
followed by `bx lr`.

The reproducible freestanding consumer may import those three functions in
addition to the original seven. Real integration must resolve all ten symbols
from the partial libc object, require an eager JUMP_SLOT target for every one,
register all ten SVC IDs to the bounded libc service, and execute all ten
consumer wrappers through the existing namespace-gated finite platform catalog
path.

The extension does not broaden the partial libc identity/SONAME or imply full
Android libc compatibility. No integer conversion, allocation, pthread, I/O,
dynamic-loader, math, errno, startup, signal, or locale behavior is introduced.


## L32-C012 — Bounded A32 atoi/strtol service

The compatibility layer may expose private service IDs `0xAB` for `atoi`
and `0xAC` for signed ARM32 `strtol`.

`atoi` consumes a logical guest string pointer in r0 and returns signed
32-bit result bits in r0. `strtol` consumes r0 input pointer, r1 optional
logical guest pointer to a little-endian 32-bit end-pointer slot, and signed r2
base, returning signed 32-bit ARM `long` bits in r0.

Parsing is bounded by caller-selected `max_parse_bytes` and implements ASCII
C whitespace, optional sign, bases 2-36 plus base 0, guarded `0x` and current
bionic `0b` prefixes, base-0 octal/decimal selection, original-input endptr
when no digits are consumed, and signed 32-bit saturation while continuing to
consume valid digits for the final end pointer.

A caller-owned `A32LibcErrnoSink` receives Android guest errno 22 (EINVAL)
for invalid base and 34 (ERANGE) for overflow/underflow. The service never
changes host process errno. A non-null strtol end-pointer is written only after
parsing succeeds; an end-pointer GuestMemory failure returns `Failed` before
errno/r0 publication.

Feature 035 adds no guest atoi/strtol shim exports, guest __errno/TLS storage,
locale-aware ctype, unsigned/wide/64-bit conversion, floating conversion, or
broader libc behavior.
