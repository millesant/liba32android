# ARM32 libdl compatibility

Status: feature 046 resident service accepted; dynamic acquisition follow-up implemented

## Goal

Provide the target-backed `libdl.so` ABI surface over the persistent ELF32
link map, with an optional bounded acquisition transaction for named objects
while keeping Android pathname/search policy and recursive unload lifecycle
outside the service itself.

The supplied FMOD image imports `dlopen`, `dlsym`, `dlclose`, and
`dlerror`. The supplied VLC ARMv7 set additionally imports `dladdr`.

## Guest ABI

The generated ARM32 `libdl.so` exports five direct SVC stubs:

- 0xBC — dlopen
- 0xBD — dlsym
- 0xBE — dlclose
- 0xBF — dlerror
- 0xC0 — dladdr

All guest handles/pointers remain logical 32-bit values. No host pointer is
published.

## Resident and dynamic dlopen

The service borrows a persistent `Elf32LinkMap` plus a finite caller-owned
handle table. Null filename acquires the first root. Without an open
transaction, named `dlopen(name, RTLD_LAZY|RTLD_NOW)` retains the accepted
resident-only exact SONAME/identity behavior.

When an `A32LibDlOpenTransaction` is supplied, named dlopen first considers
Active resident objects only. Retired tombstones are ignored. Missing names are
resolved through the caller-owned dependency provider, appended/reused as Local
persistent roots, eagerly relocated, GNU-RELRO sealed, and initialized through
persistent constructors before a synthetic handle is published.

Persistent constructors reuse the caller's once/failure lifecycle state, so
already-Complete shared dependencies are not replayed. Synchronous guest dlopen
passes trapped r13 as the nested constructor stack top.

Failures before constructor execution can remove a root added by the attempt
and reclaim newly unreachable Pending/Pending mappings while existing live
handles remain ownership anchors. Constructor-stage failures remain resident
with Failed lifecycle state because guest side effects cannot be rolled back
safely.

Repeated successful opens of the same Active object reuse one synthetic handle
and increment its finite refcount. Objects with failed constructors or
non-Pending destructor state are not reopenable.

The accepted close transaction may finalize one exact resident object on final
handle release. Recursive final-close teardown/reclamation of dependency
closures remains a separate higher ownership transaction.

## Symbol and address lookup

`dlsym(handle,name)` reuses bounded ELF32 graph lookup from the selected
resident root. `RTLD_DEFAULT` starts from the first process root and then
caller-recorded global roots. `RTLD_NEXT` is explicitly deferred.

`dladdr` locates the Active object whose loaded segment contains the guest
address, writes the ARM32 four-word `Dl_info` structure, copies a bounded
SONAME/identity into guest scratch, and scans the bounded dynamic symbol index
for the nearest preceding definition when available. Retired tombstones are
excluded from address lookup.

## Error lifetime

Errors are host-owned only until `dlerror` is called. `dlerror` copies one
pending bounded message into caller-provided guest scratch, returns that logical
guest pointer, and clears the pending error. A second call returns null until a
new error occurs.

## Real integration

The pinned-NDK fixture builds:

- `libdl.so` compatibility shim;
- `libfixture_dl_target.so` resident application DSO;
- `liba32android_libdl_consumer.so` importing both.

The integration loads the consumer into a persistent link map through an
application catalog plus namespace-gated platform `libdl.so`, eagerly
relocates it, proves direct resident-provider execution, then exercises guest
`dlopen -> dlsym -> dladdr -> dlclose -> failing dlsym -> dlerror`.

## Dynamic-acquisition validation

Focused host-side coverage exercises provider-backed missing-object acquisition,
persistent Local-root publication, eager initialization, repeated handle
refcounting, rejection of already-destructed/failed resident state,
pre-constructor RELRO failure cleanup with physical reclamation, tombstoned
same-name retry into a fresh stable slot, and constructor-failure no-replay.

The existing pinned-NDK integration remains the real ARM32 resident libdl proof;
this follow-up does not yet claim a real ARM32 missing-object dlopen fixture.

## Targeted final-close unload

An optional `A32LibDlUnloadTransaction` extends final `dlclose` beyond the
single-object lifecycle transaction.

Before guest teardown, the transaction treats every other live handle as an
ownership anchor and requires ordinary reclamation planning to report no
pre-existing unowned Active object. It then asks the read-only root-release
planner what would become unreachable if the closing object's exact persistent
root disappeared.

Only that newly unreachable vector is finalized, in
requester-before-dependency order. Exact-object teardown keeps the existing
FINI_ARRAY -> registered finalization -> DT_FINI rule. Completed object teardown
is idempotent on retry, while Failed lifecycle state remains latched.

The root and final handle remain live until every selected object is Complete.
Physical root release then performs the accepted snapshot/unmap/tombstone
transaction using the same remaining-handle anchors. A reclamation failure
keeps the root and handle, so a retry skips completed teardown and retries only
the physical release stage.

The service enables this path only for `MappedGuestMemory`, because generic
`GuestMemory` does not promise map/protect/unmap operations.

Automatic derivation of the opaque per-object `__dso_handle` binding is not
part of this transaction; embeddings still supply the object-to-DSO binding
table.

## Limits

Recursive final-close lifecycle over only newly unreachable objects,
RTLD_GLOBAL/NOLOAD/NODELETE policy, RTLD_NEXT, lazy binding, concrete Android
search paths/APK extraction, and pathname accessibility remain separate work.
