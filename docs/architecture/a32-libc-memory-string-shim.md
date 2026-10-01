# ARM32 guest libc memory/string shim/provider path

Status: features 032/034/036/038/041/043 accepted; exact-head validation PASSed

## Goal

Connect feature 030's bounded libc memory/string host services to the real ELF
dependency/symbol/relocation path using one reproducible partial ARM32
`libc.so` compatibility DSO.

The feature is deliberately partial. The accepted base grew from memory/string
helpers through allocator, EABI, finalization, synchronization, and TLS-key
extensions to 45 exports. The bounded pthread lifecycle slice adds ten more
direct guest stubs, bringing the prepared partial-libc export surface to 55.

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

A freestanding companion DSO imports the current thirty-symbol surface through ordinary function calls with builtins disabled. It therefore provides a real
`DT_NEEDED libc.so` edge plus eager ARM JUMP_SLOT relocations.

After graph load and relocation, the integration executes every exported
fixture wrapper against logical guest buffers/strings. Each call crosses:

consumer PLT -> partial libc shim -> SVC -> feature-025 registry ->
feature-030/033/035/037/040/043/045 service -> shim return -> consumer return -> requested stop PC.

## Limits

This DSO is not a replacement for Android's real libc. Its accepted surface is
still finite and omits pthread/semaphore scheduling, file/socket/stdio,
dynamic-loader APIs, libm, process startup, signals, locale, persistent C++
destructor registration, and many other libc/platform symbols. The supplied
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

A post-roadmap follow-up now supplies that bounded registration substrate:
`__aeabi_atexit` is exported by the same partial `libc.so` and dispatches
private SVC `0xD2` into caller-owned finite registration storage. The three
guest words are preserved exactly as object, destructor, and DSO-handle values;
success returns 0 and capacity exhaustion returns -1 without pretending the
destructor has executed. Registered-destructor finalization remains separate.

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

The real pinned-NDK fixture now exposes, relocates, and executes thirty-nine
partial-libc symbols. Its synchronization path covers default mutex init/lock,
EBUSY trylock, unlock/destroy, and process-local semaphore init/wait/post/destroy.

Thread creation/join, mutex attrs/types, pthread_once, condition variables,
rwlocks, TLS, pthread_self ABI, process-shared semaphores, and scheduler policy
remain outside this partial libc.


## Pthread lifecycle extension

The bounded logical pthread lifecycle slice adds ten direct private-SVC exports:
`pthread_attr_init`, `pthread_attr_destroy`,
`pthread_attr_getdetachstate`, `pthread_attr_setdetachstate`,
`pthread_attr_getstacksize`, `pthread_attr_setstacksize`,
`pthread_create`, `pthread_self`, `pthread_equal`, and
`pthread_exit`.

A dedicated freestanding ARM32 lifecycle consumer requires exactly those ten
imports through eager `R_ARM_JUMP_SLOT` relocations. Integration validates
basic attr state, logical identity/equality, two logical thread creations, one
normal start-routine return into the configured pthread_exit trampoline, and
one explicit pthread_exit path.

The original 45-wrapper partial-libc consumer remains unchanged and continues
to prove the previously accepted surface. The lifecycle consumer is separate so
new thread behavior does not broaden unrelated libc fixture assumptions.

This remains a logical/cooperative model: no host pthread is created, the
service does not map guest stacks, and join/detach reclamation plus TLS
destructor iteration remain separate work.
