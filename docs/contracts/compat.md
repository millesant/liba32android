# Compatibility contract

Status: Accepted current project contract
Last reconciled: 2026-10-01

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


## L32-C024 — Bounded ARM EABI atexit registration

The compatibility layer may expose private SVC `0xD2` for ARM
`__aeabi_atexit(object, destructor, dso_handle)`.

The service borrows a finite caller-owned span of registration records and
preserves the three AAPCS32 word arguments exactly as logical guest values:
r0 object, r1 destructor function value, and r2 DSO handle. It does not
dereference, canonicalize, or convert any of them to host pointers.

Successful registration appends one record in call order, writes zero to r0,
and returns Handled. If the caller-provided record span is full, no record is
mutated, r0 receives ARM32 -1 bits, and the service still returns Handled
because registration exhaustion is an ordinary guest-visible __cxa_atexit
failure. Unknown SVC IDs remain Unhandled.

The partial ARM32 libc shim may export `__aeabi_atexit` as a direct SVC stub.
Real integration expands the current partial-libc surface to forty symbols,
requires an eager JUMP_SLOT target for the new import, executes the wrapper,
and verifies exact object/destructor/DSO registration.

This contract provides registration state only. It does not execute registered
destructors, implement `__cxa_finalize`, choose process-exit versus DSO-unload
timing, validate destructor code, tie DSO handles to link-map objects, or reclaim
loaded mappings.


## L32-C025 — Bounded registered-destructor finalization

The accepted `A32AeabiAtexitService` registration state additionally tracks
each record as Pending, Complete, or Failed.

A host lifecycle transaction may finalize either one exact DSO handle or all
records. Matching Pending records are executed in reverse registration order.
Each callback receives the exact registered object value in guest r0 and uses
the registered destructor word as an ARM/Thumb function value. The caller
provides an 8-byte-aligned guest stack top, normalized return/stop PC, finite
instruction ceiling per callback, and a maximum callback count.

Before executing guest code, the finalizer scans the selected records. A Failed
selected record is an explicit InvalidRecordState. If the number of selected
Pending callbacks exceeds the caller ceiling, finalization fails before any
guest callback is executed or record state is mutated.

Successful callbacks become Complete and are never replayed. Invalid callback
addresses or guest memory/CPU/instruction-limit failures mark the affected
record Failed before returning, because the callback may have produced partial
guest side effects. Later finalization that selects that record fails rather
than replaying it.

A null host-side DSO selector means process-wide finalization; a concrete
selector matches the opaque registered DSO word exactly. This contract does
not yet expose guest `__cxa_finalize`, map DSO handles to link-map objects,
choose dlclose/process-exit ordering against ELF FINI_ARRAY/DT_FINI, decrement
shared-object reference counts, or unmap objects.


## L32-C026 — Guest __cxa_finalize service boundary

The compatibility layer may expose private SVC `0xD3` for guest
`__cxa_finalize(dso_handle)`.

`A32CxaFinalizeService` borrows the accepted
`A32AeabiAtexitService` registration/finalization state plus fixed bounded
finalization options. Guest r0 equal to zero selects process-wide finalization;
otherwise r0 is preserved as the exact opaque DSO-handle selector. No host
pointer conversion or link-map inference occurs.

The service delegates to the accepted reverse-order registered-destructor
finalizer. Successful completion returns Handled so the guest void function may
resume after SVC. Any bounded finalization error returns Failed and preserves
the underlying detailed finalization result for host diagnostics. Unknown SVC
IDs remain Unhandled.

This service boundary exists because Android shared-object CRT code places an
`__on_dlclose` destructor in FINI_ARRAY that calls
`__cxa_finalize(&__dso_handle)`; Android linker destruction runs FINI_ARRAY
in reverse before DT_FINI. The service itself does not yet export a
`__cxa_finalize` libc shim, make ELF lifecycle execution service-aware, bind
DSO handles to link-map objects, or own dlclose/unmapping.


## L32-C027 — Linked __cxa_finalize and service-aware ELF FINI execution

The partial ARM32 `libc.so` compatibility shim exports `__cxa_finalize` as
private SVC `0xD3`, extending the bounded shim surface from forty to forty-one
target-backed functions. A freestanding ARM32 consumer may therefore resolve an
ordinary eager JUMP_SLOT for `__cxa_finalize`.

ELF lifecycle execution may optionally receive a borrowed
`A32HostServiceHandler` and a finite per-lifecycle-call service ceiling. With
no handler, constructor/destructor execution retains the accepted direct CPU
behavior. With a handler, each lifecycle call executes through the bounded
service-dispatch loop and must still return to the normalized lifecycle stop PC
within its instruction budget.

Lifecycle execution classifies service-limit, unhandled-service, failed-service,
and suspended-service outcomes explicitly and records the failing SVC immediate
when available. Persistent lifecycle failure propagates that SVC identity and
continues to latch the affected object Failed, preserving the no-replay
guarantee.

The real partial-libc integration provides one controlled FINI_ARRAY entry in
the consumer. That FINI function calls linked `__cxa_finalize`, whose JUMP_SLOT
lands in the libc SVC stub. Service-aware FINI execution dispatches SVC 0xD3,
finalizes an exact prior `__aeabi_atexit` registration, runs its guest
destructor with the exact registered object in r0, and resumes the FINI function
to its return PC. A repeated direct `__cxa_finalize` wrapper call proves the
registration remains Complete and is not replayed.

This contract does not yet bind DSO handles to link-map ownership, decide the
last-reference dlclose transaction, reclaim mappings, or make registered
destructor callbacks themselves service-aware.


## L32-C028 — Bounded resident last-reference dlclose transaction

The resident libdl service may borrow an `A32LibDlCloseTransaction`. When no
transaction is supplied, the accepted refcount-only resident behavior remains
available. When supplied, `dlclose` delegates synthetic-handle ownership to
the transaction.

Each teardown-capable resident object has exactly one finite binding from its
stable link-map object index to one non-zero opaque guest DSO-handle word.
Non-final synthetic references decrement only. A final reference requires
Complete constructors and Pending destructors for the exact object.

The final-reference transaction executes only that object's FINI_ARRAY in
reverse order through the accepted service-aware lifecycle seam. After
FINI_ARRAY returns, every registered `__aeabi_atexit` record for the exact
bound DSO word must be Complete; no fallback callback ordering is invented.
Legacy DT_FINI then executes through the same bounded seam. Only full success
marks destructors Complete and releases the final synthetic handle.

Decode/binding/state failures before guest teardown preserve the final handle.
Guest FINI/DT_FINI failure or incomplete registered-finalization after FINI
latches destructor state Failed and preserves the handle, preventing silent
ownership loss or replay of partially executed teardown.

A focused regression proves non-final decrement, exact
FINI_ARRAY -> __cxa_finalize -> registered destructor -> DT_FINI ordering,
once-only completion, and final-handle preservation on incomplete
finalization. The real ARM32 libdl integration wires the transaction into the
ordinary dlclose SVC and proves a provider FINI_ARRAY side effect before the
synthetic handle becomes invalid.

Dependency-object recursive ownership, removal from the persistent link map,
mapping reclamation, RTLD_NODELETE/global-group policy, and dynamic
missing-object dlopen remain out of scope.


## L32-C029 — Dynamic missing-object dlopen acquisition

The resident libdl service may additionally borrow an
`A32LibDlOpenTransaction`. Without one, feature-046 resident-only named
`dlopen` behavior remains unchanged. With one, named `dlopen` delegates
initialization/ownership publication to the transaction while the service
continues to own guest string/flag/error ABI handling.

Only Active persistent-link-map slots participate in resident name/identity
lookup, `dlsym` handle roots, or `dladdr` address matching. Retired
tombstones are never reopened through resident lookup and cannot shadow a later
fresh active slot.

An Active resident object is reopenable only while destructor state is Pending
and constructor state is Pending or Complete. Failed constructors or
Complete/Failed destructor state reject new handle ownership. Repeated
successful resident opens reuse one synthetic handle and increment its bounded
refcount.

For a missing name, the transaction resolves one root through the caller-owned
dependency-provider seam, validates the provider result, and appends/reuses it
as a Local persistent root. A genuinely new identity must have synthetic-handle
capacity available before graph mutation. The loader, relocation symbol scope,
persistent lifecycle, reclamation, and libdl service ceilings must all admit the
same accumulated object count.

Objects appended by that root operation receive the accepted combined eager
main+PLT relocation transaction using the post-append persistent global scope,
then GNU RELRO sealing. Persistent constructors run dependency-first from the
root and skip already-Complete shared dependencies. When invoked synchronously
from guest `dlopen`, the transaction replaces the configured constructor stack
top with trapped live guest r13.

A failure before constructor execution removes a root record added by that
attempt and reclaims newly unreachable Pending/Pending mappings while treating
all currently live synthetic-handle objects as additional ownership anchors.
Cleanup failure is explicit.

Once constructor execution begins, failure is not rolled back: the affected
lifecycle state latches Failed and the object/root remains resident because
arbitrary guest side effects may have occurred. Handle publication occurs only
after successful initialization; if re-entrant constructor activity consumes
preflighted handle capacity before publication, the initialized root remains
resident and the open reports failure rather than discarding initialized state.

This contract does not implement recursive final-close teardown of only newly
unreachable objects, RTLD_NODELETE, RTLD_GLOBAL/LOCAL flag expansion, lazy
binding, RTLD_NEXT, or concrete pathname/APK search policy.


## L32-C030 — Targeted final-close dlclose unload

The libdl service may optionally borrow an `A32LibDlUnloadTransaction` for
physical final-close ownership. When absent, the accepted exact-object close
transaction and legacy refcount-only behavior remain available. When present,
the targeted unload path has precedence for `dlclose`.

A non-final synthetic reference decrements only.

For the final reference, every other live synthetic-handle object is treated as
an additional ownership anchor. Ordinary ownership planning must report no
pre-existing reclaimable Active objects before the transaction continues. The
transaction then performs read-only root-release planning for the exact object
owned by the closing handle. That post-release reclaimable vector is the exact
teardown set for this operation.

Selected objects finalize in deterministic requester-before-dependency order.
Each selected object reuses the accepted Android exact-object sequence:
FINI_ARRAY in reverse, completion of every registered record for the bound DSO
handle, then DT_FINI. Complete destructor state is an idempotent success so a
retry after a later selected object or physical reclamation failure never
replays completed guest teardown. Failed lifecycle state remains non-replayable.

The final synthetic handle and persistent root remain owned while lifecycle
teardown runs. Only after every selected object's destructor state is Complete
does the transaction invoke accepted physical root release/reclamation with the
same other-live-handle anchors. Reclamation failure preserves the handle/root.
Full success retires/unmaps the now-unreachable slots and clears the final
handle last.

Service delegation requires a `MappedGuestMemory` backend because physical
release owns map/protect/unmap behavior. Other `GuestMemory` implementations
fail the unload request without an unsafe backend assumption.

Object-to-DSO bindings remain caller supplied. Automatic dynamic
`__dso_handle` discovery, RTLD_NODELETE/global-group policy,
RTLD_GLOBAL/LOCAL expansion, RTLD_NEXT, lazy binding, and concurrent ownership
mutation remain separate.


## L32-C031 — Lifecycle-scoped __aeabi_atexit DSO association

`A32AeabiAtexitService` may optionally borrow finite caller-owned
`A32AeabiObjectDsoBinding` storage and the accepted lifecycle execution
context. Existing record-only construction remains valid and unchanged.

When a successful `__aeabi_atexit` call occurs with a known current lifecycle
object and non-zero opaque r2 DSO word, the service learns one
stable-object-index to DSO association. Repeated identical associations reuse
the existing binding slot. Same-object/different-DSO, same-DSO/different-object,
or learned-binding capacity conflicts return guest r0 == -1 and append neither
the registration nor binding.

A registration with no current lifecycle object or r2 == 0 remains an ordinary
accepted record but does not invent ownership provenance.

The exact-object libdl close transaction resolves a DSO from either its existing
caller-supplied explicit binding table or the learned registration state. One
source may satisfy lookup alone. Duplicate/invalid explicit entries, ambiguous
learned state, or disagreement between explicit and learned values rejects the
close as InvalidBinding.

Dynamic libdl constructor execution preserves the lifecycle-context pointer when
it overrides only the nested live stack top, so constructor-time registrations
can learn their stable object association before handle publication.

After a targeted physical unload succeeds, learned associations for the
reclaimed object indexes are forgotten deterministically. Failed lifecycle or
physical reclamation preserves them for retry. Registration records remain as
their Complete/Failed historical state and are not compacted.

Objects that never register `__aeabi_atexit`, registrations made outside a
known lifecycle object context, and process-wide exit ownership may still
require caller policy or explicit bindings.


## L32-C032 — ARM32 bionic libdl load policy

The ARM32 libdl ABI follows bionic's historical LP32 dlfcn values:
`RTLD_LOCAL=0`, `RTLD_NOW=0`, `RTLD_LAZY=1`, `RTLD_GLOBAL=2`,
`RTLD_NOLOAD=4`, `RTLD_NODELETE=0x1000`,
`RTLD_DEFAULT=0xffffffff`, and `RTLD_NEXT=0xfffffffe`.

`RTLD_LAZY` is accepted only for ABI compatibility. Android does not provide
lazy binding and LibA32Android continues to execute the accepted eager
relocation transaction before returning from a newly acquired dlopen.

Every successful named dlopen has one exact persistent Active root. This
includes opening an object already loaded as another root's dependency. Local
is the absence of Global. Global promotion is monotonic and deterministically
adds the root object to persistent global scope; a later Local open never
demotes it.

NODELETE is a separate monotonic root property. Once any successful open sets
NODELETE, later opens without the flag do not clear it.

Matching bionic's linked-object unload policy, final synthetic-handle release
for either a Global root or a NODELETE root clears the handle reference but
skips exact-object teardown, registered finalization, root release, tombstoning,
physical unmap, and learned-DSO binding retirement. This rule applies to both
the exact-object close transaction and the targeted physical-unload
transaction. A Local NODELETE root remains non-global despite being retained.

NOLOAD is resident-only. It may acquire and policy-promote an already-Active
matching object but never invokes the dependency provider or maps a missing
object. A miss returns null.

Policy publication follows synthetic-handle acquisition. If a root-policy
mutation fails before the handle is exposed to the guest, the acquired handle
reference is rolled back. Existing constructor-side-effect retention rules
continue to govern failures after guest initialization has begun.

Generated synthetic handles may not collide with either LP32 dlsym sentinel.
`RTLD_DEFAULT` retains the accepted first-root then Global-root lookup.
`RTLD_NEXT` is recognized at its ARM32 sentinel value but caller-relative
lookup remains explicitly unsupported.

NODELETE process-exit teardown, true lazy binding, RTLD_NEXT lookup, and
Android pathname/namespace search policy remain separate.


## L32-C033 — Bounded APK native-library source

`A32ApkLibrarySource` is a concrete implementation of the accepted
`A32AndroidLibrarySource` seam for exact `<archive>!/<entry>` paths. It does
not change the generic ELF dependency-provider contract.

The source requires caller-selected non-zero ceilings for virtual-path bytes,
archive bytes, ZIP entry count, central-directory bytes, and entry-name bytes.
The generic source call continues to supply the exact maximum published image
bytes.

Only ordinary single-disk ZIP32 archives are accepted. EOCD is located within
the standard ZIP comment bound. Multi-disk metadata, ZIP64 sentinel fields,
malformed/truncated directory records, and duplicate exact entry names fail
without publishing bytes.

The selected entry must use stored or DEFLATE compression and must not be
encrypted. Central metadata supplies the bounded compressed/uncompressed sizes
and CRC. The selected local header must agree on flags, method, and exact
filename; without a data descriptor it must also agree on CRC and sizes.

Stored entries publish exact payload bytes. DEFLATE entries use raw
platform/NDK zlib inflation and succeed only when the full compressed stream is
consumed, exactly the declared uncompressed byte count is produced, and CRC32
matches. Successful identity is the exact virtual path and returned bytes are
owned.

A missing archive (ENOENT/ENOTDIR) or missing exact entry returns NotFound.
Invalid options, unsupported archive/entry features, malformed structures,
resource ceilings, I/O failure, decompression failure, or integrity mismatch
return Failed.

The existing requester-scoped Android search provider composes with this source
unchanged, allowing a root such as
`/path/base.apk!/lib/armeabi-v7a` to satisfy bare DT_NEEDED lookups.

Package-manager/AssetManager discovery, split-APK policy, APK signature
verification, extraction caches, ZIP64/encrypted archive support, explicit-path
dlopen, and general archive mutation remain out of scope.


## L32-C034 — Caller-supplied APK runtime bootstrap

`A32AndroidApkRuntimeBootstrap` composes the accepted APK source,
requester-scoped Android application search, dependency-provider chain, and
`A32LibDlOpenTransaction` into one persistent caller-owned application-runtime
bootstrap. It does not add a second ELF loader.

The caller supplies one exact APK path, one relative ABI directory, one finite
complete application-local SONAME set, one existing platform dependency
provider, caller-owned mapped memory/link-map/handle/lifecycle state, and all
existing source/search/open-transaction resource options.

The application-library count and each SONAME are caller bounded. SONAMEs must
be non-empty, NUL-free, slash-free, backslash-free, and unique. The ABI
directory must be bounded and relative, must not contain NUL, backslash, or
`!`, and must not contain empty, dot, or dot-dot path components. Exact
application identities are
`<apk>!/<abi-directory>/<soname>` and must remain under both source and search
path ceilings.

The bootstrap owns one `A32ApkLibrarySource`, exact requester-root records for
every declared application identity, one
`A32AndroidLibrarySearchProvider`, one APK-local guard/root provider, an
ordered provider chain, and one `A32LibDlOpenTransaction`.

Provider order is requester-scoped application search, APK-local guard/root,
then the caller-owned platform provider. Context-free lookup therefore reaches
the guard/root provider for declared app-local dlopen/bootstrap. In
requester-aware lookup, application search gets first chance. If a declared
app-local SONAME is missing from the APK, the guard fails closed so the name
cannot be silently substituted by the platform provider. Undeclared names may
fall through to platform policy normally.

`open_root` accepts only a declared application SONAME and delegates directly
to the accepted open transaction. Persistent append, eager relocation, GNU
RELRO, dependency-first constructors, synthetic handles, Global/NODELETE
promotion, cleanup, and failure retention keep their existing semantics.

The bootstrap object must outlive consumers of its provider/open-transaction
composition. The same exposed open transaction remains usable for later
app-local named dlopen.

Package/manifest discovery, ABI auto-selection, split APK selection,
AssetManager/package-manager access, JNI startup, graphics/audio, automatic app
patching, and device deployment remain separate.


## L32-C035 — Bounded APK native-library catalog

`A32ApkLibrarySource::catalog` discovers finite application-local native
library membership for one caller-selected APK and relative ABI directory.

The catalog and exact-entry `load` operation share one private ZIP32
archive-open and central-directory parser. Single-disk structure, ZIP64
sentinels, archive bytes, ZIP entry count, central-directory bytes, and entry
name bytes therefore use one validation path rather than parallel parsers.

Catalog options require non-zero ceilings for discovered library count,
per-SONAME bytes, total published SONAME bytes, and ABI-directory bytes. The ABI
directory must be relative, bounded, NUL/backslash/! free, have no
leading/trailing slash, and contain no empty, dot, or dot-dot components.

Only direct children below `<abi-directory>/` qualify. Nested entries and
non-`.so` entries are ignored. A qualifying basename must be non-empty, bare,
NUL-free, slash-free, backslash-free, and end exactly in `.so`.

A qualifying SONAME that exceeds a catalog storage ceiling fails the operation.
Duplicate qualifying SONAMEs are ambiguous and fail. Successful output owns its
strings and is sorted lexicographically.

A missing APK returns NotFound. Invalid policy/options or malformed, truncated,
multi-disk, ZIP64-sentinel, or resource-invalid central-directory state returns
Failed through the shared parser.

Catalog enumeration does not decompress entry payloads or inspect ELF
`DT_SONAME`; ZIP entry basenames define membership for the caller-selected ABI
directory. The resulting owned strings can supply the accepted
`A32AndroidApkRuntimeBootstrap` application SONAME set. The initial root
SONAME remains an explicit caller choice.

ABI auto-detection, manifest/root selection, split-APK merging,
package-manager/AssetManager discovery, signatures, JNI startup, graphics/audio,
patching, and device deployment remain separate.


## L32-C036 — ARM32 JNI VM / JNI_OnLoad bootstrap

The first JNI compatibility surface provides one currently-attached ARM32 guest
JNI context without modeling Java objects/classes.

The caller supplies already-mapped logical guest addresses for one JavaVM
object, one eight-word JNI invocation table, one JNIEnv object, one 216-word
native-interface table, and three eight-byte ARM service stubs. Every range is
nonzero, word-aligned, pairwise non-overlapping, and must fit in the 32-bit
guest address space. Installation uses GuestMemory only, never changes mappings
or permissions, snapshots current bytes, and rolls back prior writes if a later
publication write fails.

The JavaVM object contains the invocation-table guest pointer. The invocation
table follows the Android JNI layout and publishes GetEnv at slot 6
(byte offset 0x18); unsupported invoke entries are zero. The JNIEnv object
contains its native-table guest pointer. The native table publishes FindClass at
slot 6 / byte offset `0x18` and RegisterNatives at slot 215 / byte offset
`0x35c`; unsupported native entries are zero. GetEnv, FindClass, and
RegisterNatives target distinct ARM `svc; bx lr` stubs using private SVC
immediates `0xd7`, `0xd8`, and `0xd9` respectively.

Matching Dalvik, GetEnv validates the JNI version before touching the output
slot. The inclusive numeric range from JNI_VERSION_1_1 through
JNI_VERSION_1_6 is accepted. For an in-range request, the exact installed
JavaVM pointer and a writable output slot are required; the configured guest
JNIEnv pointer is written and JNI_OK returned. An out-of-range request returns
JNI_EVERSION without modifying the output slot. The current guest execution
context is modeled as attached.

`invoke_a32_jni_on_load` resolves `JNI_OnLoad` from exactly one requested
loaded graph object using that object's bounded dynamic symbol metadata.
Dependencies and global scope never satisfy a missing exact-object symbol. The
resolved STT_FUNC is invoked as `jint JNI_OnLoad(JavaVM*, void*)` with
`r0=JavaVM*`, `r1=null`, a caller-owned aligned stack/stop PC, and finite
instruction/service-call budgets through the existing service-aware A32
executor.

When a lifecycle execution context is supplied, the exact target object index is
scoped for the complete guest invocation and the prior context is restored
afterward. Nested `__aeabi_atexit` registration therefore retains automatic
object/DSO provenance.

JNI_OnLoad succeeds only when its returned jint is exactly JNI_VERSION_1_2,
JNI_VERSION_1_4, or JNI_VERSION_1_6, matching Dalvik/ART native-library load
semantics. All other returns are an explicit unsupported-version failure.

FindClass, RegisterNatives, and the first registered-native dispatch seam are
specified by L32-C037. General Java objects/references/strings/arrays/exceptions,
member IDs and Java method/field calls, thread attach/detach, JNI_OnUnload,
framework classes, graphics, and audio remain separate.

## L32-C037 — ARM32 JNI class/native registration bootstrap

The caller owns a bounded `A32JniClassRegistry` and may associate exact
slash-separated class names with caller-selected nonzero logical 32-bit
`jclass` handles. Class names and handles are unique; accepted strings are
copied into owned host storage under explicit hard/configurable ceilings. No
host pointer is published to the guest.

`FindClass` requires the exact configured JNIEnv pointer, decodes one bounded
guest C string, returns the exact registered logical class handle on a hit, and
returns null on a semantic miss. Unreadable or unterminated guest strings are
service failures.

`RegisterNatives` requires the exact configured JNIEnv pointer and a known
class handle. It decodes ARM32 `JNINativeMethod` records as three 32-bit words
(name pointer, signature pointer, guest function pointer), validates finite
method/string limits, rejects malformed/duplicate entries, and commits only
after the complete registration call has validated. Accepted metadata owns
class/name/signature strings and stores the guest function only as a logical
32-bit value.

`invoke_a32_registered_native_noargs` resolves one exact
class/name/signature binding, accepts only zero-Java-argument signatures, seeds
`r0=JNIEnv*` and `r1=receiver-or-class`, and executes the registered ARM/Thumb
guest function through the bounded service-aware A32 executor. The raw `r0`
return bits are exposed to the caller.

The pinned-NDK ARM32 fixture performs GetEnv, FindClass, and RegisterNatives
from JNI_OnLoad, retains the registered `nativePing()I` association, and is
then invoked from the host through reverse dispatch with return value 42.
General argument marshalling, member IDs, reference lifetime, strings/arrays,
exceptions, Java method/field calls, thread attach/detach, framework classes,
graphics, and audio remain separate slices.

## L32-C038 — ARM32 JNI observed member-ID lookup

The next bounded JNI identity surface is selected from direct supplied ARMv7
machine-code evidence rather than from the complete native-interface table.

The guest JNIEnv table publishes these additional entries at their observed
Android JNI positions:

- GetMethodID — slot 33 / byte offset `0x84`;
- GetFieldID — slot 94 / byte offset `0x178`;
- GetStaticFieldID — slot 144 / byte offset `0x240`.

Each entry targets a distinct private ARM `svc; bx lr` service stub. Existing
GetEnv, FindClass, and RegisterNatives entries remain unchanged and unsupported
JNIEnv slots remain zero.

The caller-owned `A32JniClassRegistry` may seed a bounded collection of member
identities. Each member has one registered class, explicit member kind
(instance method, instance field, or static field), caller-selected unique
nonzero logical 32-bit handle, owned name/signature strings, and no host-pointer
identity. Member count, name length, and signature length obey explicit
configurable ceilings capped by hard limits.

GetMethodID, GetFieldID, and GetStaticFieldID require the exact configured
JNIEnv pointer and a registered logical class handle. Each copies bounded
NUL-terminated guest name/signature strings through GuestMemory. An exact
class/kind/name/signature match returns the configured logical handle. Unknown
class/member identity is a semantic miss and returns null. Unreadable or
unterminated guest strings fail the service.

This slice does not implement GetStaticMethodID, GetObjectClass, IsInstanceOf,
Java method invocation, field access, inheritance, Java object/reference
lifetime, strings/arrays/exceptions, or Android framework classes.

## L32-C039 — ARM32 JNI JavaVM thread attachment

Supplied ARMv7 machine code identifies JavaVM AttachCurrentThread at invocation
slot 4 / byte offset `0x10` and DetachCurrentThread at slot 5 / byte offset
`0x14`. The existing GetEnv entry remains at slot 6 / byte offset `0x18`.
Each supported entry targets a distinct private guest ARM service stub while
unsupported JavaVM entries remain zero.

The VM service models one bounded guest JNI context. Successful installation
starts that context attached so existing JNI_OnLoad behavior is preserved.

AttachCurrentThread requires the exact configured logical JavaVM pointer and a
writable guest JNIEnv** output slot. The attach-args pointer is accepted but not
interpreted in this slice. Success writes the configured logical JNIEnv pointer,
marks the modeled context attached, and returns JNI_OK. Re-attaching the same
modeled context is idempotent.

DetachCurrentThread requires the exact JavaVM pointer. Transitioning from
attached to detached returns JNI_OK. A second detach while already detached
returns JNI_ERR.

GetEnv keeps JNI-version validation ahead of output access. For a supported
version while detached it returns JNI_EDETACHED and does not modify the output
slot. JNIEnv-native services fail while the modeled context is detached.

This slice does not implement multiple host threads, JavaVMAttachArgs contents,
AttachCurrentThreadAsDaemon, Java thread objects, or thread-local reference
lifetime.

## L32-C040 — ARM32 JNI strong/local reference bookkeeping

Supplied ARMv7 machine code identifies JNIEnv NewGlobalRef at slot 21 / byte
offset `0x54`, DeleteGlobalRef at slot 22 / `0x58`, and DeleteLocalRef at
slot 23 / `0x5c`. Each entry targets a distinct private guest ARM service stub
while unsupported JNIEnv entries remain zero.

The bounded JNI registry stores opaque logical object identities with separate
local/global reference counts under explicit identity/count ceilings. No host
pointer becomes a jobject. Class registration creates a reference identity with
zero live counts; successful FindClass retains one local reference before
returning the existing logical class handle.

NewGlobalRef on null returns null. For a known live non-null identity it
increments the global count and returns the same opaque logical handle.
DeleteLocalRef and DeleteGlobalRef accept null as a no-op and otherwise decrement
only the matching reference count when present. Reference counts cannot exceed
the configured hard-capped per-handle ceiling.

The registry retains object identity metadata after counts reach zero so a later
producer such as FindClass can establish a fresh local reference. This slice
does not yet make every class/member operation a universal reference-liveness
check.

Weak references, NewLocalRef, IsSameObject, local frames, garbage collection,
cross-thread local-reference ownership, general object allocation, and full
lifetime enforcement across all JNI entrypoints remain separate.

## L32-C041 — ARM32 JNI seeded GetArrayLength

Supplied ARMv7 machine code identifies JNIEnv GetArrayLength at slot 171 / byte
offset `0x2ac`. The guest JNIEnv table publishes that exact entry through one
private ARM service stub.

The bounded JNI registry may seed finite logical array identities. Each array has
one nonzero logical handle and an exact length representable by signed 32-bit
`jsize`. Seeding creates or reuses the generic reference identity and retains
one local reference for the caller-visible array handle.

GetArrayLength requires the exact configured JNIEnv pointer, an attached JNI
context, and a registered array handle. It returns the exact seeded length in
`r0`. Null, unknown, or non-array handles fail rather than returning fabricated
metadata.

This slice does not allocate arrays, store elements, implement primitive/object
array element or region operations, expose pin/copy buffers, or infer Java array
types.

## L32-C042 — ARM32 JNI GetStaticIntField

Supplied ARMv7 machine code identifies JNIEnv GetStaticIntField at slot 150 /
byte offset `0x258`. The guest JNIEnv table publishes that exact entry through
one private ARM service stub.

The bounded JNI registry may associate one signed 32-bit static value with an
existing member whose kind is StaticField. Re-seeding the same field updates its
value. Stored field values remain bounded by the existing member-ID ceiling and
contain only caller-supplied logical Java state.

GetStaticIntField requires the exact configured JNIEnv pointer, attached state,
a registered class handle, a StaticField member handle belonging to that class,
and a seeded value. Success returns the exact `jint` bits in `r0`.
Unknown/mismatched class/member identity, wrong member kind, or missing value
fails explicitly rather than fabricating Java state.

SetStaticIntField, instance field access, Java method invocation, object
construction, inheritance, reflection, and framework-specific state remain
separate.

## L32-C043 — ARM32 JNI bounded modified-UTF-8 strings

Supplied ARMv7 machine code identifies JNIEnv NewStringUTF at slot 167 / byte
offset `0x29c`, GetStringUTFChars at slot 169 / `0x2a4`, and
ReleaseStringUTFChars at slot 170 / `0x2a8`. The guest JNIEnv table publishes
those exact entries through distinct private ARM service stubs.

The bounded JNI registry owns a finite set of logical jstring entries. A
NewStringUTF call reads one bounded NUL-terminated guest byte sequence, copies
the payload into owned storage, allocates a collision-free logical 32-bit handle
from a caller-configurable base/stride range, and retains one local reference.
Exhaustion returns null rather than exposing host identity.

The VM layout includes one caller-owned mapped writable guest scratch region for
UTF chars. GetStringUTFChars requires a known live string and no outstanding
lease, copies the stored bytes plus NUL into that region, writes JNI_TRUE through
a non-null isCopy pointer, returns the logical scratch address, and records the
leased string handle. ReleaseStringUTFChars succeeds only for the exact leased
string and exact scratch pointer, then clears the lease.

The slice preserves input bytes as supplied. It does not claim full
modified-UTF-8 validation or normalization, UTF-16 conversion, GetStringUTFLength,
region APIs, multiple simultaneous UTF-char leases, pinning semantics, or a
complete Java String implementation.

## L32-C044 — ARM32 JNI bounded jlong arrays

Supplied ARMv7 machine code identifies JNIEnv NewLongArray at slot 180 / byte
offset `0x2d0`, GetLongArrayElements at slot 188 / `0x2f0`,
ReleaseLongArrayElements at slot 196 / `0x310`, and SetLongArrayRegion at slot
212 / `0x350`. Each entry targets a distinct private ARM service stub.

The bounded registry creates zero-initialized logical jlong arrays with owned
signed 64-bit element storage, synthetic collision-free logical handles, generic
array-length metadata, and one initial local reference. Length is a signed jsize
and must be nonnegative and within the configured hard-capped element limit.

The VM layout contains one caller-owned 8-byte-aligned guest scratch region for
jlong elements. GetLongArrayElements allows one outstanding copy lease, writes
the complete array there, reports JNI_TRUE through non-null isCopy, and returns
the logical scratch address.

ReleaseLongArrayElements requires the exact leased array and exact scratch
pointer. Mode 0 copies back and releases; JNI_COMMIT (1) copies back and retains
the lease; JNI_ABORT (2) releases without copying scratch changes.

SetLongArrayRegion validates signed start/length bounds. Its fifth ARM32 C
argument is decoded from guest `[sp]` as the source buffer pointer, and guest
jlong values are decoded as little-endian 64-bit quantities.

Other primitive array families, object arrays, critical access, pinning, and
multiple simultaneous element leases remain separate.

## L32-C045 — ARM32 JNI bounded object arrays

Supplied ARMv7 machine code identifies JNIEnv NewObjectArray at slot 172 / byte
offset `0x2b0`, GetObjectArrayElement at slot 173 / `0x2b4`, and
SetObjectArrayElement at slot 174 / `0x2b8`. Each entry targets a distinct
private ARM service stub.

The bounded registry creates synthetic logical object-array handles, stores one
registered element-class handle, owns a finite vector of logical jobject
identities, publishes generic array-length metadata, and retains one local
reference for the array itself.

NewObjectArray requires a registered element-class handle, a signed nonnegative
bounded length, and a null or currently live initial logical reference. The
initial logical identity is copied into array storage without altering the
caller's local/global reference counts.

GetObjectArrayElement validates the array and signed index. A null stored element
returns null. A non-null stored logical identity gains one local JNI reference
before that same opaque logical handle is returned.

SetObjectArrayElement validates the array and signed index, accepts null, or
requires a currently live non-null logical reference before storing the opaque
identity. Array storage itself does not increment or decrement JNI local/global
reference counts.

This slice does not implement Java class assignability, ArrayStoreException,
inheritance, object construction, local frames, or general garbage collection.

## L32-C046 — ARM32 JNI bounded instance long fields

Supplied ARMv7 machine code identifies JNIEnv GetLongField at slot 101 / byte
offset `0x194` and SetLongField at slot 110 / `0x1b8`. Each entry targets a
distinct private ARM service stub.

The bounded registry may associate one signed 64-bit value with an exact
logical jobject identity and an existing InstanceField jfieldID. The object
identity must exist in the generic reference ledger and the field handle must
resolve to an InstanceField member. Existing pairs update deterministically and
new pairs obey the configured member limit.

At service time the jobject must be live through at least one local/global JNI
reference. GetLongField returns an existing value as ARM32 jlong bits in r0
(low word) and r1 (high word), and fails when the value state is absent.

SetLongField follows AAPCS32 alignment: JNIEnv, jobject, and jfieldID occupy
r0-r2, while the jlong value is decoded from guest `[sp]` / `[sp+4]` as
little-endian low/high words before the pair is created or updated.

This slice does not infer jobject class membership, Java field offsets/layout,
inheritance, volatile semantics, reflection, or other field families.

## L32-C047 — ARM32 JNI bounded ThrowNew pending state

Supplied ARMv7 machine code identifies JNIEnv ThrowNew at slot 14 / byte offset
`0x38`. The guest JNIEnv table publishes that exact entry through one distinct
private ARM service stub.

The bounded registry stores at most one pending logical exception containing a
registered class handle, owned class name, and owned message bytes. Exception
message length has one explicit hard-capped ceiling.

ThrowNew requires a live logical jclass reference and one readable bounded
NUL-terminated guest message. When no exception is pending, it copies and
records the class/message then returns JNI_OK. When one is already pending, it
returns JNI_ERR and preserves the original state.

The registry exposes a host-side clear operation so an embedding boundary or a
future ExceptionClear implementation can consume/reset pending state without
inventing a guest API.

This slice does not create Throwable jobject identity, stack traces, Java
unwinding, automatic propagation through all JNI calls, or
ExceptionOccurred/ExceptionCheck/ExceptionClear/ExceptionDescribe.

## L32-C048 — ARM32 JNI bounded CallVoidMethodV bridge

Supplied ARMv7 machine code from the VLC `libmla.so` C++ JNI wrapper identifies
JNIEnv CallVoidMethodV at slot 62 / byte offset `0xf8`. The wrapper accepts
`CallVoidMethod(jobject, jmethodID, ...)`, constructs an ARM32 `va_list`,
loads the function pointer at `0xf8`, and forwards JNIEnv, receiver, method ID,
and the `va_list` pointer in r0-r3. The guest JNIEnv table publishes only that
evidence-backed V entry in this slice; raw variadic CallVoidMethod slot 61
remains null.

CallVoidMethodV requires the exact configured JNIEnv, an attached context, a
currently live non-null logical receiver, an existing InstanceMethod ID, a valid
void-return JNI method descriptor, and a caller-owned method-call bridge.
Missing or mismatched logical state fails rather than synthesizing Java
dispatch.

The guest `va_list` decoder is bounded by an explicit hard/configured argument
ceiling. JNI boolean/byte/char/short/int arguments consume one 32-bit promoted
word. Long and double consume 8-byte-aligned little-endian 64-bit values.
Float consumes the C default-promoted double representation and is narrowed to
jfloat bits before publication. Object and array descriptors consume one
logical 32-bit reference handle; non-null reference arguments must be currently
live. Nested arrays and object descriptors are validated structurally.

Decoded values are normalized into an owned typed vector for one synchronous
callback. The callback receives copied member metadata plus borrowed argument
storage; no host pointer is published to the guest and the compatibility layer
does not invent Java implementation behavior.

Raw variadic CallVoidMethod, CallVoidMethodA, return-valued/static/nonvirtual
Call families, NewObject, class inheritance/virtual dispatch, Java frames, and
framework method implementations remain separate slices.


## L32-C049 — ARM32 JNI bounded byte-array element leases

Supplied ARMv7 `libfmod.so` machine code in
`Java_org_fmod_MediaCodec_fmodReadAt` identifies
GetByteArrayElements at JNIEnv slot 184 / byte offset `0x2e0` and
ReleaseByteArrayElements at slot 192 / `0x300`. Each entry targets one
distinct private ARM service stub.

The caller may seed a bounded logical jbyteArray with owned byte storage. The
same logical handle is registered in the generic array-length metadata and JNI
reference ledger, so GetArrayLength and local/global liveness remain
authoritative without exposing a host pointer.

The VM layout contains one caller-owned guest byte scratch region.
GetByteArrayElements requires the exact configured JNIEnv, an attached context,
a live known byte array, sufficient scratch capacity, and no outstanding byte
lease. It copies the full owned byte vector into guest scratch, writes JNI_TRUE
through a non-null isCopy pointer, returns the logical scratch address, and
records the leased array handle.

ReleaseByteArrayElements requires the exact leased array and exact scratch
pointer. Mode 0 copies scratch bytes back and releases the lease. JNI_COMMIT
(1) copies back and retains the lease. JNI_ABORT (2) releases without copying
guest changes. Unknown or dead arrays, overlapping leases, wrong pointers,
invalid release modes, configured-limit violations, and guest-memory failures
are rejected deterministically.

NewByteArray, GetByteArrayRegion, SetByteArrayRegion, other primitive-array
families, pinning, Java framework behavior, and host-pointer publication remain
separate.

## L32-C050 — ARM32 JNI bounded raw CallVoidMethod bridge

Supplied VLC ARMv7 `libvlcjni.so` machine code in
`VLCJniObject_attachEvents` identifies raw variadic CallVoidMethod at JNIEnv
slot 61 / byte offset `0xf4`. The call site places JNIEnv, receiver, method
ID, and the first promoted variadic word in r0-r3, stages later arguments on
the guest stack, and converts one float source to an 8-byte-aligned promoted
double stack argument.

The guest JNIEnv table publishes slot 61 through one distinct private ARM
service stub while preserving the accepted CallVoidMethodV slot 62 behavior.

CallVoidMethod requires exact configured JNIEnv/attached state, a currently
live non-null logical receiver, an existing InstanceMethod ID, a structurally
valid void-return JNI method descriptor, and the caller-owned method-call
bridge. Missing or mismatched logical state fails rather than synthesizing Java
dispatch.

The raw AAPCS32 decoder shares the accepted bounded descriptor/value parser.
The first promoted 32-bit integral/reference value consumes r3; later 32-bit
values consume successive guest stack words. A jlong, jdouble, or
default-promoted jfloat cannot begin in odd r3, so it advances directly to the
next 8-byte-aligned guest stack location and consumes two little-endian words.
Object and array descriptors produce logical 32-bit handles and every non-null
decoded reference must be live.

Decoded values use the same synchronous `A32JniValue` bridge vector as
CallVoidMethodV. No host pointer is published to the guest.

CallVoidMethodA, return-valued/static/nonvirtual call families, NewObject,
inheritance/virtual dispatch, Java frames, and framework method
implementations remain separate slices.

## L32-C051 — ARM32 JNI bounded instance int field reads

Supplied VLC ARMv7 `libvlcjni.so` machine code identifies
`GetIntField` at JNIEnv slot 100 / byte offset `0x190` in
`Java_org_videolan_libvlc_Media_nativeNewFromFd`. The call forwards JNIEnv,
jobject, and cached jfieldID in r0-r2 and consumes the jint result from r0.

The registry stores caller-seeded signed 32-bit values keyed by a logical
jobject handle plus an existing InstanceField ID. Existing pairs update
deterministically under the bounded member-state ceiling. Storage never derives
or exposes a host Java object address.

`GetIntField` requires the exact configured JNIEnv, an attached context, a
currently live logical jobject, an existing InstanceField member ID, and an
existing seeded value for that exact object/member pair. It returns the signed
32-bit value bit-for-bit in ARM32 r0. Missing, dead, mismatched, or wrong-kind
state fails rather than fabricating a Java field.

Object-class assignability, Java field layout/offsets, inheritance, volatile
semantics, `SetIntField`, reflection, and other field families remain
separate slices.

## L32-C052 — ARM32 JNI bounded exception observation and clear

Supplied VLC ARMv7 `libvlcjni.so` machine code identifies
`ExceptionOccurred` at JNIEnv slot 15 / byte offset `0x3c` and
`ExceptionClear` at slot 17 / byte offset `0x44` in
`Java_org_videolan_libvlc_Media_nativeNewFromFd`.

A successful ThrowNew reserves one bounded logical exception handle in a
dedicated namespace with zero initial local/global JNI reference counts. The
pending root keeps that logical exception identity available for observation
without exposing a host pointer or reusing the jclass handle.

`ExceptionOccurred` requires the exact configured JNIEnv and an attached
context. With no pending exception it returns null. Otherwise it retains one
local JNI reference to the exact pending exception handle, returns that handle
in ARM32 r0, and leaves pending state intact.

`ExceptionClear` requires the exact configured JNIEnv and an attached context.
It is an empty-state no-op. When an exception is pending, it clears the pending
root while preserving local/global references already returned for that
exception. If the pending identity was never observed and has zero JNI
reference counts, clear reclaims that reserved identity immediately. A later
ThrowNew does not alias an exception identity that remains in the logical
reference ledger.

ExceptionCheck, ExceptionDescribe, Java stack traces, Java-frame unwinding,
automatic pending-exception gating of unrelated JNI calls, and framework
exception behavior remain separate slices.

## L32-C053 — ARM32 JNI bounded NewObjectV bridge

Supplied ARMv7 `libmla.so` machine code from the weak C++
`_JNIEnv::NewObject(_jclass*, _jmethodID*, ...)` wrapper identifies JNIEnv
`NewObjectV` at slot 29 / byte offset `0x74`. The wrapper constructs an
ARM32 `va_list` and forwards JNIEnv, jclass, jmethodID, and that `va_list`
in r0-r3. Raw `NewObject` slot 28 and `NewObjectA` slot 30 remain null in
this slice.

`NewObjectV` requires the exact configured JNIEnv, an attached context, a
currently live logical jclass, an existing InstanceMethod ID owned by that
exact class and named `<init>`, a valid void-return method descriptor, and
the existing embedding-owned method-call bridge. Constructor arguments reuse
the bounded descriptor / ARM32 `va_list` decoder accepted for
`CallVoidMethodV`; every non-null reference argument must be a live logical
identity.

The embedding bridge may return one fresh nonzero logical jobject handle. The
registry rejects collisions or invalid identities and creates exactly one local
reference for an accepted result. No host object pointer is published to the
guest.

Raw `NewObject`, `NewObjectA`, Java heap/object layout, class
assignability/inheritance, constructor bytecode execution, and framework object
behavior remain separate slices.

## L32-C054 — ARM32 JNI bounded GetStaticMethodID

Supplied VLC ARMv7 `libvlcjni.so` JNI_OnLoad directly loads JNIEnv byte
offset `0x1c4`, slot 113, before repeated indirect calls with JNIEnv, jclass,
method-name, and signature pointers in r0-r3. This establishes
`GetStaticMethodID` from machine-code evidence rather than table adjacency.

The bounded member registry models StaticMethod as a distinct kind appended to
the existing member-kind domain, preserving the established InstanceMethod,
InstanceField, and StaticField values. Static and instance methods with the
same class/name/signature therefore remain distinct logical identities.

`GetStaticMethodID` requires the exact configured JNIEnv, an attached
context, a known logical jclass, and bounded readable NUL-terminated method-name
and signature strings. Exact StaticMethod class/name/signature matches return
the caller-seeded logical jmethodID. Unknown classes or members are semantic
misses and return null; guest memory faults fail the service.

Static method invocation, Java dispatch/inheritance, class initialization,
reflection, and framework behavior remain separate slices.

## L32-C055 — ARM32 JNI bounded raw CallStaticVoidMethod bridge

Supplied VLC ARMv7 `libvlcjni.so` directly identifies
`CallStaticVoidMethod` at JNIEnv slot 141 / byte offset `0x234`.
The raw call forwards JNIEnv, jclass, and cached static jmethodID in r0-r2;
r3 is the first variadic Java argument and remaining words continue on the
guest stack.

The service requires the exact configured JNIEnv, an attached context, a live
logical jclass, and an existing StaticMethod ID belonging to that exact class.
It reuses the accepted bounded raw descriptor/AAPCS32 decoder. Every non-null
decoded reference argument must be a live logical JNI identity.

Normalized values cross one synchronous caller-owned static-void bridge
boundary. Bridge rejection fails the service; success returns void. No host
pointer, Java implementation, class initialization, or dispatch behavior is
synthesized.

`CallStaticVoidMethodV/A`, `CallStaticObjectMethod`, return-valued static
families, Java inheritance/dispatch, and framework behavior remain separate
evidence-driven slices.

## L32-C056 — ARM32 JNI bounded raw CallStaticObjectMethod bridge

Supplied VLC ARMv7 `libvlcjni.so` directly identifies raw
`CallStaticObjectMethod` at JNIEnv slot 114 / byte offset `0x1c8`.
The table publishes that exact entry through one private ARM service stub.

The service requires the exact configured JNIEnv, an attached context, a live
logical jclass, and an existing StaticMethod ID belonging to that class. The
accepted raw r3-plus-stack AAPCS32 decoder now distinguishes object/array return
descriptors from the void-return contract retained by existing void-call and
constructor paths. Non-null decoded reference arguments must be live.

The caller-owned synchronous bridge may return null or a pre-existing logical
JNI reference identity. A non-null result must already exist in the registry
and receives exactly one local reference before guest exposure. Unknown return
identities fail instead of creating implicit Java heap state.

`CallStaticObjectMethodV/A`, other static return families, Java class
initialization/dispatch, framework object creation, and broad Java heap
semantics remain separate evidence-driven slices.

## L32-C057 — ARM32 JNI bounded weak global references

Supplied VLC ARMv7 `libvlcjni.so` provides balanced lifecycle evidence for
`NewWeakGlobalRef` at JNIEnv slot 226 / byte offset `0x388` and
`DeleteWeakGlobalRef` at slot 227 / `0x38c`.

The native table extends through slot 227 only. The bounded reference ledger
tracks weak ownership separately from local/global strong counts on the same
logical object handle. `NewWeakGlobalRef` accepts null as null and otherwise
requires a currently strong-live known identity; it increments only the weak
count. A weak-only identity does not satisfy strong-liveness checks and cannot
seed `NewGlobalRef`.

`DeleteWeakGlobalRef` accepts null as a no-op and otherwise requires and
decrements existing weak ownership without changing local/global counts.
Pending-exception cleanup preserves an identity that still has weak ownership.

Garbage collection, automatic weak clearing, resurrection,
`NewLocalRef` from jweak, `IsSameObject`, local frames, and general Java
heap reachability remain separate evidence-driven work.

## L32-C058 — ARM32 bounded JNI_OnUnload invocation

Supplied VLC ARMv7 `libmla.so` and `libvlcjni.so` export concrete
`JNI_OnUnload(JavaVM*, void*)` entrypoints. The latter directly uses the
already-supported JavaVM `GetEnv` and JNIEnv `DeleteGlobalRef` services
during cleanup.

`invoke_a32_jni_on_unload` targets one exact already-loaded dependency-graph
object. It builds that object's bounded symbol index and resolves
`JNI_OnUnload` only inside the target object; dependencies and global scope
never satisfy a missing symbol. The resolved symbol must be a non-null STT_FUNC
with valid ARM/Thumb entry alignment.

Execution uses r0 for the configured logical JavaVM address, r1=null, a
caller-owned aligned stack, stop PC, instruction ceiling, and service-call
ceiling. Optional lifecycle provenance is scoped to the exact object for the
duration of the call and restored on every return.

The entrypoint returns void, so success is bounded service-aware execution that
reaches the caller stop PC; no JNI version result is interpreted. Invalid
options/objects/functions and memory, CPU, service, suspension, or instruction
failures remain explicit.

This slice exposes explicit invocation only. Automatic invocation is not tied
to generic ELF `dlclose`: JNI library unload is a VM/class-loader lifecycle
concern and requires a separate ownership/ordering contract.

## L32-C059 — ARM32 JNI bounded ExceptionCheck observation

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly loads JNIEnv byte offset
`0x390`, slot 228, calls that function pointer, compares r0 against zero, and
branches on the returned jboolean. This identifies `ExceptionCheck`.

The guest JNIEnv table extends exactly through slot 228 and publishes one
distinct private ARM service stub for `ExceptionCheck`. The service requires
the exact configured JNIEnv and attached bounded JNI context.

`ExceptionCheck` is a pure observation of the existing bounded pending-
exception state: it returns JNI_FALSE when no exception is pending and JNI_TRUE
when pending state exists. It does not allocate or retain a jthrowable
reference, clear state, alter reference counts, or mutate the stored
class/message identity.

Existing `ExceptionOccurred` and `ExceptionClear` semantics remain
unchanged. `ExceptionDescribe`, automatic exception gating across unrelated
JNI calls, Java stack traces/unwinding, and framework exception behavior remain
outside this slice.

## L32-C060 — ARM32 JNI bounded GetStaticObjectField

Supplied VLC ARMv7 `libvlc.so` JNI_OnLoad directly identifies
`GetStaticObjectField` at JNIEnv slot 145 / byte offset `0x244`. The
observed path first resolves a static jfieldID through slot 144, calls slot 145
with JNIEnv/jclass/jfieldID, then passes the returned jobject to
GetStringUTFChars and later DeleteLocalRef.

The guest JNIEnv table publishes slot 145 through one distinct private ARM
service stub while leaving unsupported entries null.

The bounded registry may associate an explicitly seeded null or pre-existing
logical jobject identity with an existing StaticField member ID. The stored
field identity is Java/static state, not a caller JNI local/global reference
count. A non-null seed must already exist in the bounded logical reference
ledger.

GetStaticObjectField requires the exact configured JNIEnv, attached state, a
registered class, a StaticField member belonging to that class, and seeded
state. A seeded null returns null. A seeded non-null identity gains exactly one
local JNI reference before the same opaque logical handle is returned.
Deleting an earlier returned local reference does not erase the stored static
field identity; a later read can create a new local reference.

Unknown seeded identities, wrong class/member kind, and missing values fail
deterministically. SetStaticObjectField, Java class initialization, field
descriptor type enforcement, inheritance/assignability, garbage collection,
reachability, and framework object semantics remain separate evidence-driven
slices.


## L32-C061 — ARM32 bounded pthread TLS keys

Supplied VLC ARMv7 `libmla.so` has eager libc `R_ARM_JUMP_SLOT` imports for
`pthread_key_create`, `pthread_key_delete`, `pthread_getspecific`, and
`pthread_setspecific`.

The compatibility layer may expose private SVC IDs `0x100` through `0x103`
for exactly those four APIs. These immediates intentionally avoid the existing
libdl, libm, lifecycle, and JNI private-service ranges.

`A32PthreadSyncService` borrows finite caller-owned logical-key metadata and
finite per-logical-thread value metadata. Keys are deterministic non-zero
32-bit identities and do not mirror bionic private key representation or expose
host pthread/TLS objects. The embedding selects one non-zero logical thread ID
before executing a guest thread.

`pthread_key_create` writes the new logical key to guest memory, preserves a
guest destructor callback address only as metadata, and returns Android
`EAGAIN=11` when key capacity is exhausted.

`pthread_setspecific` updates or clears only the current logical-thread value
for an active key. Invalid or deleted keys return Android `EINVAL=22`; finite
value-metadata exhaustion returns Android `ENOMEM=12`.
`pthread_getspecific` returns the selected logical-thread value or null,
including null for invalid/deleted keys.

`pthread_key_delete` invalidates the key and clears every stored value for
that key without executing the recorded destructor. Re-deleting or otherwise
mutating an invalid key returns Android `EINVAL=22`.

The prepared partial ARM32 `libc.so` may export the four functions as direct
private-SVC stubs. The current real integration resolves 45 prepared exports,
requires eager `R_ARM_JUMP_SLOT` targets for them, and executes all four TLS
wrappers end-to-end through the bounded service.

Thread-exit destructor execution/iteration, `pthread_create`,
`pthread_join`, `pthread_detach`, `pthread_self`, `pthread_equal`,
cancellation, scheduler lifecycle, host TLS, and broader bionic pthread
semantics remain separate work.


## L32-C062 — ARM32 bounded pthread logical lifecycle

Supplied ARM32 FMOD/VLC artifacts require `pthread_create`,
`pthread_self`, `pthread_equal`, `pthread_exit`, and the basic
creation-attribute subset `pthread_attr_init/destroy`,
`pthread_attr_setdetachstate`, and `pthread_attr_get/setstacksize`.
The compatibility surface additionally accepts `pthread_attr_getdetachstate`
as the paired read operation.

Private SVC IDs `0x104` through `0x10D` implement that bounded surface.
Guest `pthread_t` values are deterministic opaque non-zero 32-bit logical
identities. Internally they share the numeric identity of
`runtime::A32LogicalThreadId`; this is not a public C ABI and never exposes a
host pthread ID or pointer.

`pthread_create` allocates finite caller-owned lifecycle metadata plus one
page-aligned first-fit stack range from a caller-configured already-mapped
guest arena. The service never maps stack memory itself. The new logical A32
context receives the start argument in r0, an 8-byte-aligned guest SP, and the
configured pthread_exit guest stub in LR, so normal start-routine return
preserves its r0 result into pthread_exit.

Creation publication is transactional: failed output-memory publication,
identity allocation, or stack allocation commits no logical thread/stack state.
Finite thread/stack exhaustion returns Android `EAGAIN`.

`pthread_self` returns the selected logical identity and `pthread_equal`
compares identities. `pthread_exit` records the 32-bit guest return value and
terminates through the existing cooperative service-suspension boundary rather
than creating or joining a host thread.

The prepared partial ARM32 libc shim exports these ten lifecycle functions as
direct private-SVC stubs. A dedicated ARM32 consumer resolves and executes
them separately from the established 45-wrapper base libc integration.

## L32-C063 — ARM32 pthread join/detach and bounded thread-exit cleanup

Supplied VLC ARMv7 libraries have eager libc imports for `pthread_join`, and
the shipped ARMv7 `libc++_shared.so` imports both `pthread_join` and
`pthread_detach`. Private SVC `0x10E` implements pthread_join and
`0x10F` implements pthread_detach.

The lifecycle state preserves Bionic-observable ownership errors:
self-join returns Android `EDEADLK=35`; an unknown/reclaimed target returns
`ESRCH=3`; detached or already-claimed join state returns `EINVAL=22`.

Joining a live joinable target claims it exactly once, records an optional
guest return-value address, publishes eventual guest return code zero, and
returns `Suspended` without blocking the host executor. Successful target
exit publishes its stored return value and marks one deterministic wake record.
The embedding pops that wake and resumes the joiner from its preserved post-SVC
continuation; popping also reclaims the target exactly once. Joining an
already-exited target publishes its optional value and reclaims synchronously.

Detaching a running joinable target changes ownership without host action.
Detaching an already-exited joinable target reclaims synchronously. A detached
target reclaims its lifecycle/owned-stack metadata immediately after successful
exit cleanup. Reclaimed thread slots and stack ranges are deterministically
reusable.

When a lifecycle service borrows `A32PthreadSyncService`, pthread_exit runs
bounded TLS destructor cleanup before publishing Exited/reclaimable state.
For each active key with nonzero destructor and non-null value, the value is
cleared before executing the guest destructor on the exiting logical thread's
live stack. The callback may repopulate TLS through the ordinary pthread TLS
service. Scanning repeats for at most four destructor rounds, matching Bionic's
`PTHREAD_DESTRUCTOR_ITERATIONS`; any remaining per-thread TLS values are then
discarded. pthread_key_delete continues to clear values without destructor
execution.

Destructor callbacks execute through bounded A32 service-aware execution with
explicit instruction/service ceilings. This slice exposes the pthread
synchronization/TLS service to those callbacks; it does not imply arbitrary
JNI/Android nested services.

After TLS cleanup, an optional compatibility-layer thread-exit hook may run for
future per-thread JNI/reference cleanup. No JNI semantics are implemented by
this contract. Only then does the target become Exited, wake a joiner, or
reclaim if detached.

Cleanup failure latches a non-replayable CleanupFailed thread state with
bounded diagnostic classification; a later pthread_exit attempt does not rerun
partially executed destructors. Join-result publication failure after cleanup
also latches failure rather than silently losing ownership.

The prepared partial libc shim therefore exposes 57 functions total: the
existing 45-wrapper base surface plus twelve pthread lifecycle stubs. The
dedicated lifecycle consumer requires twelve eager JUMP_SLOT imports and proves
live join suspension/wake/post-SVC resume plus detach reclamation. Cancellation,
signals, condition variables, rwlocks, pthread_once, robust/process-shared
semantics, host pthread lifecycle, and actual JNI thread cleanup remain out of
scope.


## L32-C064 — ARM32 bounded pthread condition variables

Supplied VLC ARMv7 `libc++_shared.so`, `libvlc.so`, and `libvlcjni.so`
have eager libc imports across `pthread_cond_init/destroy`,
`pthread_cond_wait`, `pthread_cond_timedwait`, `pthread_cond_signal`, and
`pthread_cond_broadcast`. No supplied ARM32 artifact imports pthread condattr
functions.

Private SVC IDs `0x110` through `0x115` implement exactly those six
condition operations. Condition addresses remain opaque nonzero logical
identities; no Bionic private pthread_cond_t representation or host condition
variable is stored or exposed.

Condition waits reuse the existing finite `A32PthreadWaiter` table and the
existing logical mutex ownership/grant path. A wait reserves its condition
waiter before releasing the associated mutex. Capacity failure therefore does
not release the caller's mutex. Releasing the mutex may wake an already queued
ordinary mutex waiter.

Signal transfers the oldest condition waiter into the existing mutex
reacquisition queue; broadcast transfers all current waiters in deterministic
arrival order. A signaled/timed waiter is not published runnable until it owns
the associated mutex. Ordinary mutex waiters and condition reacquirers compete
through the same deterministic sequence/grant mechanism.

Current Bionic `pthread_cond_destroy` does not reject active waiters; the
bounded compatibility service likewise returns success rather than inventing
EBUSY for application-undefined concurrent destruction.

`pthread_cond_timedwait` consumes the Bionic LP32 timespec ABI: signed
32-bit seconds followed by signed 32-bit nanoseconds. Nanoseconds outside
[0,1_000_000_000) return `EINVAL=22`; negative seconds return
`ETIMEDOUT=110` before mutex release. A null timeout pointer behaves as an
untimed wait.

Default condition timed waits use CLOCK_REALTIME through an embedding-owned
`A32PthreadClock`; compatibility code does not read host wall time directly.
The service exposes the next condition deadline and an explicit deadline poll,
allowing deterministic fake-clock tests with no host sleeps.

When a deadline expires, the waiter transitions exactly once into the same
mutex-reacquisition path with eventual return value ETIMEDOUT. The wake record
carries that return value so the embedding can patch r0 in the saved post-SVC
continuation before resume. Signal/broadcast polls expired timed waits first,
giving the bounded single-threaded model one deterministic deadline-first rule
for timeout-vs-signal races.

The prepared partial libc shim now has 63 exports total: the original 45 base
wrappers, twelve pthread lifecycle functions, and six condition-variable
functions. The dedicated ARM32 pthread fixture requires eighteen lifecycle /
condition JUMP_SLOT imports and exercises wait/signal/reacquire/resume plus a
fake-clock timed timeout end to end.

Supplied VLC ARMv7 libraries also import `clock_gettime`. That guest-visible
libc clock API is not part of this condition-variable contract and remains
separate evidence-backed utility work. Process-shared condvars, cond attrs,
cancellation points, rwlocks, pthread_once, and host futex/condvar passthrough
remain out of scope.


## L32-C065 — ARM32 pthread once, rwlocks, and typed mutexes

Supplied ARM32 FMOD/VLC/libc++ binaries directly import
`pthread_mutexattr_init/settype/destroy`, `pthread_once`, the existing
mutex init/destroy/lock/trylock/unlock family, and VLC's
`pthread_rwlock_init/destroy/rdlock/wrlock/unlock`.

Private SVCs `0x116` through `0x121` extend the bounded pthread
synchronization service. `0x11A` is reserved for an internal pthread_once
completion trampoline and is not a guest libc API.

Mutex attributes are finite opaque metadata keyed by guest attr address. This
contract accepts NORMAL/DEFAULT=0, RECURSIVE=1, and ERRORCHECK=2 only.
pthread_mutex_init copies the selected type into finite logical mutex metadata.
Unknown mutex addresses locked without explicit init remain NORMAL lazy static
initializers.

Recursive mutexes track logical owner and bounded acquisition depth. Owner
relock/trylock succeeds and increments depth until the accepted 65536-acquisition
ceiling; the next acquisition returns EAGAIN. Final unlock alone transfers
ownership. Error-check owner relock returns EDEADLK and owner trylock returns
EBUSY. Non-owner unlock of RECURSIVE or ERRORCHECK mutexes returns EPERM.
No mutexattr getter, pshared, protocol, priority inheritance/protection, robust
state, or host pthread object is introduced.

pthread_once state is finite metadata keyed by guest once-control address.
The first caller transitions Uninitialized -> Initializing and becomes logical
owner. The service saves the post-SVC continuation and redirects guest
execution into the initializer. The initializer returns through a configured
internal guest trampoline that traps SVC 0x11A; completion marks Done, wakes
all once waiters, restores the saved continuation, and returns pthread_once
success without rerunning the initializer.

A concurrent caller during Initializing suspends cooperatively in the existing
bounded waiter table. Calls after Done return immediately. Nested once controls
owned by one logical thread complete in last-started order.

If initializer guest execution faults or terminates before the completion
trampoline, the embedding calls `fail_once_initialization(thread_id)`.
Owned Initializing controls latch Failed and are not replayed. Subsequent calls
fail host-service execution; already suspended once waiters remain suspended
because the owning guest execution has failed. The compatibility layer does not
fabricate a POSIX pthread_once error result.

Rwlocks use finite opaque metadata with one logical writer owner, bounded
reader count, and the shared waiter table. No host rwlock/futex identity is
stored. The accepted default follows current Bionic behavior: while readers
hold the lock, additional readers may acquire despite a pending writer. Once
the rwlock becomes fully unlocked, pending writers are preferred; one writer
is granted before readers, otherwise all pending readers are granted together.
This defines deterministic wake eligibility only and does not claim global
fairness or starvation freedom.

Blocking read/write acquisition by the current writer returns EDEADLK; the
corresponding try calls return EBUSY. A non-owner writer unlock returns EPERM.
Destroy of an owned or waited-on rwlock returns EBUSY. Write-after-read remains
a potential cooperative deadlock because read ownership is aggregate, matching
the relevant Bionic behavior rather than inventing per-reader identity.

The issue contract also accepts bounded pthread_rwlock_tryrdlock and
pthread_rwlock_trywrlock, although the supplied ARM32 artifacts do not directly
import those two functions. Timed rwlocks, rwlock attrs, process-shared locks,
priority policy, cancellation, and robust recovery remain out of scope.

The prepared partial libc shim now contains 74 libc-compatible exports: the
original 45 base wrappers, twelve lifecycle functions, six condition-variable
functions, and eleven common synchronization functions. It additionally
exports one internal once-completion trampoline. The dedicated ARM32 pthread
consumer requires 32 eager JUMP_SLOT imports and executes recursive typed mutex,
pthread_once, concurrent rwlock readers, blocked writer wake, and post-SVC
writer resume end to end.
