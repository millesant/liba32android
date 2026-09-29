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

