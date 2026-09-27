# Compatibility contract

Status: Accepted current project contract
Last reconciled: 2026-09-27

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
in r2/r3. Both explicit lengths are bounded by `max_transfer_bytes`. An empty
needle returns the haystack pointer without validating or reading either byte
range; a haystack shorter than a non-empty needle returns null without validating
or reading either byte range. Only a search that can inspect bytes validates the
required logical ranges; wrap or GuestMemory read failure then returns
`Failed`. Otherwise the first exact byte match returns a logical guest pointer.

`strcpy` reads the complete source including NUL into temporary storage before
destination mutation. The source payload must terminate within
`max_string_bytes`, the full copy including NUL must fit
`max_transfer_bytes`, and the destination logical range must not wrap.

`strncpy` bounds its explicit count by `max_transfer_bytes`. Zero count
performs no guest access. Non-zero calls read at most count source bytes, stop
reading after NUL and pad the temporary destination with NULs, or copy exactly
count bytes without inventing a terminator when no NUL is encountered. The
full destination count range is validated before mutation; source addressability
is checked only for bytes actually read before NUL/count, so padding bytes never
require a fictitious source range.

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


## L32-C013 — Partial libc integer shim extension

The prepared partial ARM32 `libc.so` may additionally export `atoi` and
`strtol` using shared feature-035 SVC IDs `0xAB` and `0xAC`. The guest
stubs preserve incoming AAPCS32 arguments, execute only their SVC, and return
with `bx lr`.

The freestanding consumer may import both functions. Real integration must
resolve all twelve prepared partial-libc symbols from the shim, require a
JUMP_SLOT target for each, register the ten memory/string/copy/search SVCs plus
the two integer SVCs, and execute real `atoi` and `strtol` wrappers through
the existing namespace-gated finite platform catalog.

The strtol integration must verify a logical guest end-pointer write. Guest
errno storage remains outside the shim; the caller-owned feature-035 errno sink
remains the error publication seam.

No additional libc symbol or full-libc/application compatibility claim is
introduced by feature 036.


## L32-C014 — Guest errno slot and __errno service

The compatibility layer may expose private SVC `0xAD` for bionic
`int* __errno(void)` through caller-owned `A32LibcGuestErrnoState`.

One state is configured with one non-zero logical guest address for a four-byte
errno slot. The `__errno` service returns that logical address in r0 and never
exposes a host pointer.

`A32LibcErrnoSink` receives `GuestMemory` and returns publication success.
`A32LibcGuestErrnoState` implements the sink by writing signed errno bits
little-endian into its configured guest slot. Feature-035 integer conversion
must return `Failed` if required errno publication fails.

The embedding must use a distinct state/slot per guest thread when threading is
implemented. Feature 037 does not model Android TLS layout, `__get_tls`,
pthread TLS keys, or other thread-local libc storage.


## L32-C015 — Partial libc __errno shim extension

The prepared partial ARM32 `libc.so` may additionally export `__errno` as a
guest SVC `0xAD` stub. The freestanding consumer may import `__errno` and
provide a wrapper that dereferences the returned guest `int*`.

Real integration must resolve thirteen partial-libc symbols and require a
JUMP_SLOT target for each. The exact-SVC registry must map `0xAD` to the same
`A32LibcGuestErrnoState` used as feature-035 integer-service errno sink.

An overflowing real guest `strtol` call must write Android ERANGE into the
configured guest errno slot, and a subsequent real guest `__errno` wrapper
must read that same value through the returned logical guest pointer.

No Android TLS layout, guest thread selection, or additional libc symbols are
introduced by feature 038.


## L32-C016 — Android 17 release alignment baseline

Android-facing compatibility semantics are audited against the official
`android-latest-release` manifest. As of 2026-09-27 it selects
`android17-release`; stable Android 17.0.0 r1 source is the exact-source
reference for bionic/linker behavior in the feature-029 through feature-038
prepared lineage.

The audit preserves the direct linked-namespace accessibility model, Android
log-write ABI, ARM32 atoi/strtol rules, memmem empty-needle result, and
per-guest-thread errno-pointer abstraction. It corrects two prepared feature-033
access-order mismatches: memmem fast paths must precede unused-range validation,
and strncpy padding after NUL must not require source addressability for bytes
that are never read.

Future Android-semantic changes must record the release/ref used as evidence
rather than relying on unqualified `main`/`master` behavior.


## L32-C017 — Bounded logical guest heap

The compatibility layer may expose private SVC IDs `0xAE` through `0xB1`
for ARM32 `malloc`, `calloc`, `realloc`, and `free`.

`A32LibcGuestHeap` borrows one already mapped writable logical guest arena,
finite caller-owned allocation metadata, and the existing guest errno sink. It
owns no guest mapping and never exposes host allocation pointers.

Placement is deterministic first-fit with 16-byte minimum alignment. Zero-size
allocation receives a minimum internal block when capacity exists. `calloc`
detects 32-bit size multiplication overflow and zeroes exactly the requested
bytes. Allocation exhaustion/overflow returns null and publishes Android
ENOMEM=12.

`realloc(nullptr,n)` follows allocation; `realloc(ptr,0)` frees an exact live
pointer and returns null. Growth preserves the minimum old/new requested payload
and a failed growth leaves the old allocation live. `free(nullptr)` is a
no-op. Unknown non-null free/realloc pointers return `Failed`.

Scudo internals, page ownership, locking, aligned allocation, usable-size,
mallinfo/mallopt, and C++ allocation operators remain outside this contract.

## L32-C018 — Partial libc allocator shim extension

The prepared partial ARM32 `libc.so` may additionally export `malloc`,
`calloc`, `realloc`, and `free` as the shared private SVC stubs
`0xAE` through `0xB1`. The freestanding consumer may import all four
functions alongside the thirteen already prepared partial-libc symbols.

Real integration must resolve all seventeen symbols, require an eager
`R_ARM_JUMP_SLOT` target for each, and route the allocator quartet to one
`A32LibcGuestHeap` that shares the same guest errno state used by integer
conversion. The heap arena must be already mapped, writable guest memory and
all returned pointers remain logical 32-bit guest addresses.

The integration must execute real guest `malloc`, `realloc`, `calloc`,
and `free` wrappers, prove realloc payload preservation and calloc zeroing,
and retain the existing namespace-gated finite platform-catalog path.

This remains a bounded partial libc compatibility shim. It does not add
Android TLS/thread scheduling, constructor/destructor lifecycle, aligned
allocation, stdio/file/socket I/O, libdl, libm, startup, signals, locale, or a
full Android libc claim.

## L32-C019 — ARM EABI memory helpers and memmove extension

The bounded memory/string service may additionally expose private SVC `0xB2`
for `memmove(dest, src, count)`. It uses the existing
`max_transfer_bytes` ceiling, rejects logical source/destination range wrap,
performs no guest access for zero count, and stages the complete source payload
before the destination write so overlapping ranges have memmove semantics.
The service returns the logical destination in r0 on ordinary non-zero success.

The prepared partial ARM32 `libc.so` may additionally export plain `memmove`
plus the twelve bionic ARM EABI memory helpers:

`__aeabi_memcpy`, `__aeabi_memcpy4`, `__aeabi_memcpy8`,
`__aeabi_memmove`, `__aeabi_memmove4`, `__aeabi_memmove8`,
`__aeabi_memset`, `__aeabi_memset4`, `__aeabi_memset8`,
`__aeabi_memclr`, `__aeabi_memclr4`, and `__aeabi_memclr8`.

The memcpy and memmove helper variants preserve r0/r1/r2 as
destination/source/count and dispatch to the corresponding bounded service.
The memset helpers implement the ARM EABI argument order
`(destination, count, value)` by reordering r1/r2 before dispatching to the
existing libc memset service. The memclr helpers implement
`(destination, count)` by dispatching memset with byte value zero. The
alignment-suffixed 4/8 variants have the same observable behavior as their
unsuffixed bionic counterparts; no stronger guest alignment precondition is
invented by the compatibility shim.

The freestanding consumer and real integration must resolve all thirty current
partial-libc symbols, require an eager `R_ARM_JUMP_SLOT` target for each, and
execute all thirty wrappers through the existing namespace-gated finite
platform catalog. Integration must prove overlapping memmove, EABI memset
argument order, and EABI memclr zeroing.

`__aeabi_atexit` is explicitly outside this feature because Android routes it
into C++ destructor registration and persistent lifecycle state rather than a
stateless memory primitive.

## L32-C020 — Bounded pthread mutex/semaphore synchronization

The compatibility layer may expose private SVC IDs `0xB3` through `0xBB`
for `pthread_mutex_init`, `pthread_mutex_destroy`,
`pthread_mutex_lock`, `pthread_mutex_trylock`,
`pthread_mutex_unlock`, `sem_init`, `sem_destroy`, `sem_wait`, and
`sem_post`.

`A32PthreadSyncService` borrows finite caller-owned mutex, semaphore, and
waiter metadata. Guest synchronization object pointers remain opaque logical
32-bit addresses and are used only as identity keys; the compatibility layer
does not expose host pthread objects or mirror bionic private object layouts.
The embedding selects one non-zero logical thread ID before executing each
guest thread.

Unknown mutex addresses used by lock/trylock are treated as default static
mutex initializers. Explicit mutex init accepts only a null attr in this slice.
An uncontended lock acquires ownership synchronously. Trylock on an owned mutex
returns Android `EBUSY=16`. A contended blocking lock records one finite
waiter, sets the eventual guest return value to zero, and returns the runtime
`Suspended` disposition. Unlock transfers ownership to the oldest waiter
before publishing that logical thread as ready, so resumption continues after
the original lock SVC without replay.

`sem_init` supports process-local semaphores only (`pshared == 0`) with a
bounded non-negative 32-bit count. `sem_wait` consumes an available count
synchronously or suspends one finite waiter at zero. `sem_post` grants the
oldest waiter without incrementing the count, otherwise increments up to
`0x7fffffff`. The embedding drains ready-thread records and owns scheduling.

The prepared partial ARM32 `libc.so` may export the nine functions above as
minimal SVC stubs. The real fixture resolves thirty-nine partial-libc symbols,
requires one eager `R_ARM_JUMP_SLOT` target for each, and executes all
thirty-nine wrappers.

This feature does not implement pthread creation/join/detach, attrs beyond
null mutex attrs, recursive/errorcheck mutex types, condition variables,
rwlocks, pthread_once, TLS keys, pthread_self ABI, process-shared semaphores,
signals, host futexes, or a complete scheduler.

## L32-C021 — Resident-object ARM32 libdl compatibility

The compatibility layer may expose private SVC IDs `0xBC` through `0xC0`
for ARM32 `dlopen`, `dlsym`, `dlclose`, `dlerror`, and `dladdr`.

`A32LibDlService` borrows one caller-owned persistent `Elf32LinkMap`, a
finite handle table, and caller-owned guest scratch buffers for `dlerror` and
`Dl_info` strings. Synthetic guest handles are logical 32-bit opaque values
chosen from a caller-selected range; host pointers are never exposed.

This slice is resident-object only. `dlopen` accepts `RTLD_LAZY` or
`RTLD_NOW` and may acquire/refcount only an object already present in the
persistent link map by exact SONAME/identity, or the first root for a null
filename. A missing resident object returns null and records a bounded
`dlerror`. No filesystem/APK search, provider acquisition, new ELF mapping,
relocation, constructor execution, or namespace mutation occurs inside the
service.

`dlsym` resolves a synthetic handle through the existing bounded graph-local
symbol lookup. `RTLD_DEFAULT` searches the first root closure followed by
caller-recorded global roots. `RTLD_NEXT` is explicitly unsupported and
reports an error. `dlclose` decrements only the synthetic handle refcount;
loaded objects remain mapped and no destructor/unload behavior is implied.

`dlerror` copies one pending bounded error string into caller-provided guest
scratch and clears it after one read. `dladdr` identifies the loaded object
whose PT_LOAD range contains the guest address, publishes ARM32
`Dl_info { dli_fname, dli_fbase, dli_sname, dli_saddr }` through logical guest
pointers, and may report the nearest preceding dynamic symbol using the
existing bounded symbol index/string-table readers.

A reproducible freestanding ARM32 `libdl.so` shim exports the five functions
as direct private-SVC stubs. Real integration must load it through the existing
namespace-gated finite platform catalog beside one application-resident target
DSO, eagerly relocate the consumer, execute all five shim paths, prove
resident-object `dlopen`/exact `dlsym`, `dladdr` module/symbol metadata,
refcount-only `dlclose`, and clear-on-read `dlerror`.

Dynamic acquisition of missing objects, RTLD_GLOBAL/NOLOAD/NODELETE semantics,
RTLD_NEXT caller-relative lookup, unload/refcount-driven FINI_ARRAY execution,
persistent constructor/destructor called-state, and Android filesystem/search
policy remain deferred.

## L32-C022 — Shared target-backed ARM32 libm compatibility

The compatibility layer may expose private SVC IDs `0xC1` through `0xD1`
for the seventeen libm symbols observed in both supplied ARM32 target sets:
`acos`, `asin`, `atan2`, `cos`, `cosf`, `exp`, `floor`,
`frexp`, `ldexp`, `log`, `log10`, `log10f`, `pow`, `powf`,
`sin`, `sinf`, and `tan`.

The service implements the Android armeabi-v7a base soft-float calling
convention explicitly through AAPCS32 core registers. A double occupies one
little-endian register pair; a second double occupies r2/r3. Float values use
one register word. `frexp` receives its logical guest `int*` in r2 and
writes the signed exponent little-endian through `GuestMemory`; `ldexp`
receives its signed exponent bits in r2. Results are returned in r0 or r0/r1
as appropriate. No host pointer is exposed.

Host `libm` may be used as the numerical engine for this bounded compatibility
slice, but every call must preserve the embedding process's pre-call `errno`
and floating-point environment. Guest errno/fenv exception publication is not
invented by this feature.

A reproducible freestanding ARM32 `libm.so` shim exports all seventeen names
as direct private-SVC stubs. Its freestanding consumer is built explicitly
with ARM softfp ABI and ordinary dynamic imports. Real integration must load
the shim through the existing requester-aware Android namespace/platform
catalog, require seventeen eager `R_ARM_JUMP_SLOT` relocations, and execute
all seventeen guest wrappers with exact stable reference cases including
`frexp` guest exponent publication.

This feature does not claim complete Android `libm.so`, vector/complex math,
the larger VLC-only math surface, guest floating-point exception flags,
guest errno behavior for math domain/range errors, alternate rounding-mode
semantics, or architecture-specific bionic assembly equivalence.

## L32-C023 — Requester-scoped Android app-library search

The compatibility layer may provide a bounded requester-aware application
library search provider above the generic ELF dependency-provider contract.

`A32AndroidLibrarySearchProvider` borrows a finite ordered span of search-root
records, one `A32AndroidLibrarySource`, and a non-zero `max_path_bytes`
ceiling. Each root binds one exact opaque requester identity to one virtual
library directory. A dependency request is eligible only when requester
identity is non-empty and the requested dependency is a bare non-empty name
containing no NUL, forward slash, or backslash.

For each root whose requester identity matches byte-for-byte, the provider
constructs `root + "/" + requested_name` (without duplicating an existing
trailing slash), rejects an empty/malformed root or candidate exceeding
`max_path_bytes`, and calls the borrowed source with the candidate path plus
the exact generic `max_image_bytes` ceiling.

Source `NotFound` falls through to the next matching root; source `Failed`
terminates as provider failure. Success requires a non-empty opaque identity,
a non-empty image, and image size not exceeding the forwarded ceiling before
the result is published to the generic loader. If no matching source succeeds,
the provider returns `NotFound`. Context-free `resolve` therefore returns
`NotFound` unless a future caller explicitly supplies requester context.

The source abstraction owns concrete filesystem, APK/ZIP, asset-manager, or
other byte acquisition. This feature performs no host I/O itself and does not
interpret `RUNPATH`/`RPATH`, `LD_LIBRARY_PATH`, namespace permitted paths,
explicit slash-containing dlopen paths, transitive namespace links, platform
library allowlists, or dynamic unload policy.

A reproducible ARM32 integration fixture must model an APK-style virtual root
`base.apk!/lib/armeabi-v7a`: a root DSO has one ordinary `DT_NEEDED` child,
the search provider acquires that child through requester-scoped virtual-path
lookup, the loader forms a two-object graph, eager `R_ARM_JUMP_SLOT`
relocation targets the searched child, and real A32 execution reaches the child
implementation successfully.
