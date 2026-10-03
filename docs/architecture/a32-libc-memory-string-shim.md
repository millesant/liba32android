# ARM32 guest libc memory/string shim/provider path

Status: features 032/034/036/038/041/043 plus __cxa_atexit registration accepted; exact-head validation pending

## Goal

Connect feature 030's bounded libc memory/string host services to the real ELF
dependency/symbol/relocation path using one reproducible partial ARM32
`libc.so` compatibility DSO.

The feature is deliberately partial. The base consumer now resolves 47
libc-compatible exports after adding __cxa_atexit to the existing
memory/string, allocator, EABI, finalization, synchronization, and TLS-key
surface. The dedicated pthread lifecycle fixture remains separate. Across the
current shim's later pthread, signal, and scheduler extensions, the public
surface is 92 libc-compatible exports plus the internal pthread_once completion
trampoline.

## Guest stubs

The freestanding ARM-mode shim has SONAME `libc.so` and exports:

- `memcpy` -> SVC `0xA1`
- `memset` -> SVC `0xA2`
- `memcmp` -> SVC `0xA3`
- `memchr` -> SVC `0xA4`
- `strlen` -> SVC `0xA5`
- `strcmp` -> SVC `0xA6`
- `strncmp` -> SVC `0xA7`
- `memmem` -> SVC `0xA8`
- `strcpy` -> SVC `0xA9`
- `strncpy` -> SVC `0xAA`
- `atoi` -> SVC `0xAB`
- `strtol` -> SVC `0xAC`
- `__errno` -> SVC `0xAD`

Each function consists only of its shared feature-030 SVC followed by
`bx lr`. The guest and host consume the same preprocessor-safe service-ID
header.

## Provider

`make_a32_libc_memory_string_shim_catalog_entry` creates a borrowed exact-name
catalog entry for SONAME `libc.so` with stable identity
`liba32android-compat-libc-memory-string`.

The real integration places that entry in feature 031's finite platform
catalog. Feature 029 namespace policy exposes exact `libc.so` from the
consumer's `app` namespace to the `platform` namespace before dependency
loading may acquire the shim.

## Real consumer

A freestanding companion DSO imports the current 47-symbol base surface through
ordinary function calls with builtins disabled. It therefore provides a real
`DT_NEEDED libc.so` edge plus eager ARM JUMP_SLOT relocations.

After graph load and relocation, the integration executes every exported
fixture wrapper against logical guest buffers/strings. Each call crosses:

consumer PLT -> partial libc shim -> SVC -> feature-025 registry ->
feature-030/033/035/037/040/043/045 service -> shim return -> consumer return -> requested stop PC.

## Limits

This DSO is not a replacement for Android's real libc. Its accepted surface is
still finite and omits pthread/semaphore scheduling, file/socket/stdio,
dynamic-loader APIs, libm, process startup, stdio/file/socket APIs, locale,
the broader C++ runtime, and many other libc/platform symbols. The supplied
FMOD/VLC binaries still cannot be claimed loadable from this partial shim
alone.

## Allocator extension (feature 041)

Feature 041 extends the same reproducible partial `libc.so` and freestanding
consumer with `malloc`, `calloc`, `realloc`, and `free`. The four guest
functions are minimal A32 SVC stubs using the shared feature-040 IDs
`0xAE`- `0xB1`; the host integration binds those IDs to one
`A32LibcGuestHeap` over an already mapped writable guest arena.

The real integration now requires seventeen eager `R_ARM_JUMP_SLOT` targets
and executes all seventeen wrappers. Allocator execution proves that returned
values are logical guest addresses, realloc preserves the staged payload,
calloc clears exactly the requested bytes, and free completes through the same
SVC path. The existing `libc.so` SONAME, catalog identity, namespace gate, and
finite provider model are unchanged.

This does not make the shim a complete Android libc. Thread/TLS selection,
constructor/destructor lifecycle, aligned-allocation extensions, I/O/stdio,
libdl, libm, startup, signals, locale, and device execution remain separate
work.

## ARM EABI memory extension (feature 043)

Feature 043 adds plain `memmove` at private SVC `0xB2` and twelve ARM EABI
memory-helper exports required by the supplied VLC ARMv7 native set:
`__aeabi_memcpy{,4,8}`, `__aeabi_memmove{,4,8}`,
`__aeabi_memset{,4,8}`, and `__aeabi_memclr{,4,8}`.

The memcpy/memmove forms are direct SVC stubs. The memset forms reorder the ARM
EABI arguments from `(dest, count, value)` into the existing libc memset
service convention before SVC. The memclr forms transform `(dest, count)`
into a zero-valued memset call. Each helper remains a void-return ABI function;
the shim does not establish any caller-visible return-value contract.

The real fixture now requires thirty eager JUMP_SLOT targets and executes all
thirty wrappers. Tests exercise overlapping plain/EABI memmove, all 4/8
variants, EABI memset argument order, and EABI memclr zeroing.

`__aeabi_atexit` is not part of feature 043 because it participates in C++
static-destructor registration and requires a persistent lifecycle contract.

A post-roadmap follow-up supplies that bounded registration substrate:
`__aeabi_atexit` is exported by the same partial `libc.so` and dispatches
private SVC `0xD2` into caller-owned finite registration storage. The three
guest words are preserved exactly as object, destructor, and DSO-handle values;
success returns 0 and capacity exhaustion returns -1.

The evidence-backed `__cxa_atexit(destructor, object, dso_handle)` entrypoint
now reuses exactly that storage and SVC. Its ARM shim swaps r0/r1 before
dispatch so the host service continues to receive
`(object, destructor, dso_handle)`; no second registration path or state
table exists. `__cxa_finalize` continues to consume the same records in
reverse registration order under the existing bounded lifecycle contract.

## Pthread mutex/semaphore extension (feature 045)

Feature 045 extends the same partial `libc.so` with nine process-local
synchronization functions: the five default mutex operations
`pthread_mutex_{init,destroy,lock,trylock,unlock}` and
`sem_{init,destroy,wait,post}`.

The guest functions remain minimal SVC stubs. Host-side state is finite and
caller-owned; guest mutex/semaphore addresses are opaque logical identities and
no host pthread object is exposed. Uncontended operations return synchronously.
Contended mutex lock and zero-count sem_wait use feature 044's `Suspended`
runtime disposition. A later unlock/post grants the oldest waiter before the
embedding resumes its saved post-SVC A32 state.

At the feature-045 boundary the real pinned-NDK fixture exposed, relocated,
and executed thirty-nine
partial-libc symbols. Its synchronization path covers default mutex init/lock,
EBUSY trylock, unlock/destroy, and process-local semaphore init/wait/post/destroy.

Thread creation/join, mutex attrs/types, pthread_once, condition variables,
rwlocks, TLS, pthread_self ABI, process-shared semaphores, and scheduler policy
remain outside this partial libc.


## Pthread lifecycle extension

The bounded logical pthread lifecycle surface adds twelve direct private-SVC
exports: `pthread_attr_init`, `pthread_attr_destroy`,
`pthread_attr_getdetachstate`, `pthread_attr_setdetachstate`,
`pthread_attr_getstacksize`, `pthread_attr_setstacksize`,
`pthread_create`, `pthread_self`, `pthread_equal`, `pthread_exit`,
`pthread_join`, and `pthread_detach`.

A dedicated freestanding ARM32 lifecycle consumer requires exactly those
twelve imports through eager `R_ARM_JUMP_SLOT` relocations. Integration
validates basic attr state and logical identity/equality, then proves a live
join suspension, target exit and return-value publication, wake/reclamation,
post-SVC join continuation, and a running detach followed by detached exit
reclamation.

The base 47-wrapper partial-libc consumer continues
to prove the previously accepted surface. The lifecycle consumer is separate so
new thread behavior does not broaden unrelated libc fixture assumptions.

This remains a logical/cooperative model: no host pthread is created and the
service does not map guest stacks. Join/detach reclamation and bounded
four-pass TLS destructor cleanup are owned by the pthread lifecycle service;
rwlocks, pthread_once, cancellation, guest-visible clock APIs, and broader
Bionic surface remain separate work.


## Pthread condition-variable extension

The partial libc shim adds six direct private-SVC exports:
`pthread_cond_init`, `pthread_cond_destroy`, `pthread_cond_wait`,
`pthread_cond_timedwait`, `pthread_cond_signal`, and
`pthread_cond_broadcast`.

The dedicated ARM32 pthread consumer therefore requires eighteen lifecycle /
condition imports in total. Integration executes condition init, a real wait
that suspends and is signaled, post-SVC waiter resume only after mutex
reacquisition, a deterministic fake-clock timed wait returning ETIMEDOUT,
broadcast, and destroy.

The base 47-wrapper libc fixture remains the independent base integration.
The condition-variable slice itself still owns no guest clock API. The later
bounded `clock_gettime` service reuses the same embedding-owned logical clock
source, so timed waits and guest clock reads stay deterministic without host
wall-clock access inside compatibility code.


## Pthread common-synchronization extension

The partial libc shim adds eleven public compatibility stubs for
`pthread_mutexattr_init/destroy/settype`, `pthread_once`, and
`pthread_rwlock_init/destroy/rdlock/tryrdlock/wrlock/trywrlock/unlock`.

An additional exported internal symbol,
`__liba32android_pthread_once_complete`, is a runtime trampoline rather than a
libc compatibility API. Its first instruction traps private once-completion
SVC 0x11A so the synchronization service can publish Done and restore the
original pthread_once continuation.

The dedicated ARM32 pthread consumer now requires 32 eager JUMP_SLOT imports:
the prior 18 lifecycle/condition symbols plus the evidenced mutex-attribute,
mutex operation, once, and rwlock symbols. The real integration executes a
recursive mutex through attributes, verifies pthread_once completes only once,
and exercises concurrent readers followed by a suspended/resumed writer.

The try-rwlock stubs are retained as bounded coherent companions required by
the accepted issue contract, but supplied-binary evidence is not claimed for
those two symbols.

## Pthread utility extension

The partial libc shim additionally exports three direct private-SVC wrappers:
`pthread_getschedparam`, `pthread_setschedparam`, and `pthread_setname_np`.
These are the only new utility symbols directly evidenced by the supplied
ARM32 libraries after scanning every `armeabi-v7a` object in the VLC APK.

The dedicated ARM32 pthread consumer now requires 35 eager JUMP_SLOT imports
and executes the three wrappers against bounded logical lifecycle state. The
scheduler surface is deliberately synthetic: SCHED_OTHER/0 is queryable and
settable, while broader host scheduler policy is not exposed. Thread names are
bounded logical metadata and are not forwarded to host pthreads.

The supplied `libemu32.so` is AArch64, so its semaphore and pthread-attribute
utility imports are not used to broaden this AArch32 shim. The original
45-wrapper base libc consumer remains backward compatible.

## Pthread cleanup-handler extension

The partial libc shim adds direct private-SVC wrappers for
`__pthread_cleanup_push` and `__pthread_cleanup_pop`, matching the two
symbols imported by supplied ARM32 VLC `libvlc.so`.

The cleanup record stays caller-owned guest memory; the runtime retains only
the logical thread's 32-bit top record address and bounded depth. A real ARM32
fixture pushes one cleanup record, pops it with execute enabled, and verifies
the guest callback result. Separate focused lifecycle coverage verifies LIFO
exit unwinding and cleanup-before-TLS-destructor ordering.

The dedicated ARM32 pthread consumer therefore requires 37 eager JUMP_SLOT
imports and performs 43 wrapper calls. The base 47-wrapper libc consumer continues as the independent base
integration. No pthread cancellation symbol is added.

## Signal compatibility extension

The partial libc shim adds direct private-SVC wrappers for `raise`,
`sigaction`, `sigpending`, `pthread_sigmask`, and `sigwait`, matching
the supplied ARM32 FMOD/VLC import evidence.

The dedicated ARM32 pthread consumer requires 42 eager JUMP_SLOT imports.
Its real integration queries the default SIGPIPE action, blocks SIGPIPE,
generates it through `raise`, verifies `sigpending`, consumes it through
`sigwait`, and restores the original mask. The harness now performs 49
host-side wrapper calls.

Signal state is logical and bounded: masks/pending bits are attached to logical
pthread state, while dispositions and waiters remain inside the compat signal
service. No host signal or host thread identity is exposed. The original
45-wrapper base libc consumer remains unchanged.

## Scheduler/affinity compatibility extension

The partial libc shim adds direct private-SVC wrappers for `setpriority`,
`sched_get_priority_max`, `sched_get_priority_min`, `sched_getaffinity`,
`sched_setscheduler`, and `sched_yield`.

The dedicated ARM32 pthread consumer now requires 48 eager JUMP_SLOT imports
and performs 55 host-side wrapper calls. Integration checks realtime priority
query bounds, one-CPU synthetic ARM32 affinity, the accepted
`SCHED_OTHER/0` mutation, logical current-thread nice state, and a real
`sched_yield` suspension followed by post-SVC continuation.

No wrapper calls the host scheduler or affinity APIs. The base 47-wrapper
libc consumer continues as the independent base integration.

